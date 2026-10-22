import os
import random
import numpy as np
import cv2
from PIL import Image, ImageEnhance

# Configuration - Point these to your existing folders
INPUT_DIR = "scanning_method/scanned_cards_rgba" 
TRAIN_DIR = "scanning_method/final_dataset/train"
VAL_DIR = "scanning_method/final_dataset/val"

# 50 total new images (40 train, 10 val)
NEW_TRAIN_COUNT = 40
NEW_VAL_COUNT = 10
TOTAL_NEW = NEW_TRAIN_COUNT + NEW_VAL_COUNT

def match_real_lighting(pil_image):
    """Reduces blown-out whites and harsh colors to match the real image."""
    # 1. Drastically reduce contrast (flattens harsh colors)
    enhancer_contrast = ImageEnhance.Contrast(pil_image)
    img_low_contrast = enhancer_contrast.enhance(random.uniform(0.65, 0.75))
    
    # 2. Reduce brightness (turns pure white into the muted grey seen in reality)
    enhancer_brightness = ImageEnhance.Brightness(img_low_contrast)
    img_muted = enhancer_brightness.enhance(random.uniform(0.6, 0.75))
    
    return img_muted

def apply_real_camera_artifacts(cv_image):
    """Applies the soft focus and subtle noise seen in the real capture."""
    # The real image is slightly soft/out of focus
    blurred = cv2.GaussianBlur(cv_image, (3, 3), 0)
    
    # Add subtle sensor noise (lower variance than before, as the real image isn't heavily grainy)
    var = random.uniform(5, 15) 
    sigma = var ** 0.5
    noise = np.random.normal(0, sigma, blurred.shape)
    noisy_image = np.clip(blurred + noise, 0, 255).astype(np.uint8)
    
    return noisy_image

def process_and_append_card(img_path, card_name):
    card_img = Image.open(img_path).convert("RGBA")
    
    train_class_dir = os.path.join(TRAIN_DIR, card_name)
    val_class_dir = os.path.join(VAL_DIR, card_name)
    
    # Ensure directories exist just in case
    os.makedirs(train_class_dir, exist_ok=True)
    os.makedirs(val_class_dir, exist_ok=True)
    
    for i in range(TOTAL_NEW):
        # 1. Spatial Jitter (Keep this for the FPGA cropper logic)
        base_rotation = random.choice([0, 90, 180, 270])
        fine_rotation = random.uniform(-3, 3) 
        rotated_card = card_img.rotate(base_rotation + fine_rotation, expand=True, resample=Image.BICUBIC)
        
        # 2. Correct the Lighting & Contrast
        muted_card = match_real_lighting(rotated_card)
        
        # 3. Match Real Background Color (Medium warm grey, not black)
        # Based on the edges of real_IMG_20260810_142216_BURST046.jpg
        bg_color = (
            random.randint(65, 85), # R
            random.randint(60, 80), # G
            random.randint(60, 80), # B
            255
        )
        
        # Tight crop canvas
        bg_size = (muted_card.width + 10, muted_card.height + 10)
        background = Image.new("RGBA", bg_size, bg_color)
        
        paste_x, paste_y = random.randint(3, 7), random.randint(3, 7)
        background.paste(muted_card, (paste_x, paste_y), muted_card)
        
        # 4. Apply Camera Artifacts
        cv_image = cv2.cvtColor(np.array(background), cv2.COLOR_RGBA2RGB)
        final_cv_image = apply_real_camera_artifacts(cv_image)
        
        # 5. Save and Route (Split 40 to train, 10 to val)
        final_output = Image.fromarray(final_cv_image)
        
        # Use a unique prefix to guarantee no manual curations are overwritten
        filename = f"{card_name}_v2_synth_{i}.png" 
        
        if i < NEW_TRAIN_COUNT:
            final_output.save(os.path.join(train_class_dir, filename), "PNG")
        else:
            final_output.save(os.path.join(val_class_dir, filename), "PNG")

def main():
    card_files = [f for f in os.listdir(INPUT_DIR) if f.endswith('.jpg')]
    for card_file in card_files:
        card_name = os.path.splitext(card_file)[0]
        print(f"Appending 50 corrected images for: {card_name}...")
        process_and_append_card(os.path.join(INPUT_DIR, card_file), card_name)
        
    print("Append complete. Existing files were not modified.")

if __name__ == "__main__":
    main()