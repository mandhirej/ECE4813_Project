import os
import random
import numpy as np
import cv2
from PIL import Image, ImageEnhance, ImageDraw

# Configuration
INPUT_DIR = "scanning_method/scanned_cards_rgba" 
OUTPUT_DIR = "scanning_method/augmented_dataset_tightcrop"
AUGMENTATIONS_PER_CARD = 50

def apply_motion_blur(cv_image):
    """Simulates the card still settling or a hand moving away."""
    kernel_size = random.choice([3, 5, 7])
    kernel = np.zeros((kernel_size, kernel_size))
    # Randomize blur direction (horizontal, vertical, or diagonal)
    if random.choice([True, False]):
        kernel[int((kernel_size-1)/2), :] = np.ones(kernel_size)
    else:
        np.fill_diagonal(kernel, 1)
    kernel /= kernel_size
    return cv2.filter2D(cv_image, -1, kernel)

def add_edge_occlusion(pil_image):
    """Simulates a finger/thumb on the edge, strictly avoiding corner pips."""
    draw = ImageDraw.Draw(pil_image)
    w, h = pil_image.size
    # Occlude middle of the edges, not the corners
    x_pos = random.choice([0, w - random.randint(15, 25)])
    y_pos = random.randint(int(h * 0.3), int(h * 0.7))
    
    # Draw a dark, thumb-like ellipse
    draw.ellipse([x_pos, y_pos, x_pos + 25, y_pos + 40], fill=(30, 20, 20, 255))
    return pil_image

def process_card(img_path, output_folder, card_name):
    card_img = Image.open(img_path).convert("RGBA")
    
    for i in range(AUGMENTATIONS_PER_CARD):
        # 1. Discrete Rotations + Tight Crop Jitter
        base_rotation = random.choice([0, 90, 180, 270])
        fine_rotation = random.uniform(-5, 5) # Reduced perspective skew
        rotated_card = card_img.rotate(base_rotation + fine_rotation, expand=True, resample=Image.BICUBIC)
        
        # 2. Add Edge Occlusion before background merge
        if random.random() > 0.7:
            rotated_card = add_edge_occlusion(rotated_card)
        
        # 3. Simulate Tight Hardware Crop (Minimal background margin)
        bg_color = (random.randint(10, 25), random.randint(10, 25), random.randint(10, 25), 255)
        jitter_x, jitter_y = random.randint(-5, 5), random.randint(-5, 5)
        
        # Canvas is barely larger than the card
        bg_size = (rotated_card.width + 10, rotated_card.height + 10)
        background = Image.new("RGBA", bg_size, bg_color)
        
        # Paste with minimal jitter
        paste_x = 5 + jitter_x
        paste_y = 5 + jitter_y
        background.paste(rotated_card, (paste_x, paste_y), rotated_card)
        
        # 4. Low-Light & Motion Blur
        cv_image = cv2.cvtColor(np.array(background), cv2.COLOR_RGBA2RGB)
        
        if random.random() > 0.6:
            cv_image = apply_motion_blur(cv_image)
            
        # Add calibrated noise (assuming you will tune this variance)
        var = random.uniform(15, 30) 
        sigma = var ** 0.5
        noise = np.random.normal(0, sigma, cv_image.shape)
        noisy_image = np.clip(cv_image + noise, 0, 255).astype(np.uint8)
        
        # 5. Save Lossless
        final_output = Image.fromarray(noisy_image)
        filename = f"{card_name}_tight_{i}.png"
        final_output.save(os.path.join(output_folder, filename), "JPEG")

def main():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        
    card_files = [f for f in os.listdir(INPUT_DIR) if f.endswith('.jpg')]
    for card_file in card_files:
        card_name = os.path.splitext(card_file)[0]
        card_folder = os.path.join(OUTPUT_DIR, card_name)
        if not os.path.exists(card_folder):
            os.makedirs(card_folder)
        process_card(os.path.join(INPUT_DIR, card_file), card_folder, card_name)

if __name__ == "__main__":
    main()