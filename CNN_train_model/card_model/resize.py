import os
import cv2
from pathlib import Path
from tqdm import tqdm

# Configuration
SOURCE_DIR = Path("scanning_method/final_dataset")
TARGET_DIR = Path("scanning_method/final_dataset_64x64")

def resize_dataset():
    if not SOURCE_DIR.exists():
        print(f"Error: Directory {SOURCE_DIR} not found.")
        return

    # Find all images in the train and val subfolders
    image_paths = list(SOURCE_DIR.rglob("*.*"))
    valid_extensions = {'.png', '.jpg', '.jpeg'}
    image_paths = [p for p in image_paths if p.suffix.lower() in valid_extensions]
    
    print(f"Found {len(image_paths)} images. Starting resize...")
    
    for img_path in tqdm(image_paths, desc="Resizing"):
        img = cv2.imread(str(img_path))
        if img is None:
            continue
            
        # INTER_AREA is mathematically optimal for aggressive downsampling
        resized = cv2.resize(img, (64, 64), interpolation=cv2.INTER_AREA)
        
        # Mirror the folder structure (train/class_name or val/class_name)
        relative_path = img_path.relative_to(SOURCE_DIR)
        target_path = TARGET_DIR / relative_path
        
        # Ensure the subdirectories exist before saving
        target_path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(target_path), resized)
        
    print(f"\nDone! Resized images successfully saved to: {TARGET_DIR}")

if __name__ == "__main__":
    resize_dataset()