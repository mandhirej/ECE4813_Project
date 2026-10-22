import os
import cv2
from pathlib import Path
from sklearn.model_selection import train_test_split

# Configuration
BLACKFOAM_DIR = Path("scanning_method/augmented_dataset_blackfoam")
TRAIN_DIR = Path("scanning_method/final_dataset/train")
VAL_DIR = Path("scanning_method/final_dataset/val")

def extract_card(img_path):
    img = cv2.imread(str(img_path))
    if img is None:
        return None
        
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    _, thresh = cv2.threshold(blurred, 50, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return img # Fallback if no contour is found
    
    largest_contour = max(contours, key=cv2.contourArea)
    x, y, w, h = cv2.boundingRect(largest_contour)
    return img[y:y+h, x:x+w]

def append_blackfoam_dataset():
    if not BLACKFOAM_DIR.exists():
        print(f"Error: Directory {BLACKFOAM_DIR} not found.")
        return

    classes = [d.name for d in BLACKFOAM_DIR.iterdir() if d.is_dir()]
    total_processed = 0
    
    for cls in classes:
        cls_dir = BLACKFOAM_DIR / cls
        img_files = list(cls_dir.glob("*.*"))
        
        if not img_files:
            continue
            
        # Split the files 80/20 to match your existing dataset ratio
        train_files, val_files = train_test_split(img_files, test_size=0.2, random_state=42)
        
        # Ensure destination directories exist
        train_out_dir = TRAIN_DIR / cls
        val_out_dir = VAL_DIR / cls
        train_out_dir.mkdir(parents=True, exist_ok=True)
        val_out_dir.mkdir(parents=True, exist_ok=True)
        
        # Process and save Train images
        for f in train_files:
            cropped = extract_card(f)
            if cropped is not None:
                # Add a unique prefix to avoid overwriting existing data
                save_path = train_out_dir / f"blackfoam_{f.name}"
                cv2.imwrite(str(save_path), cropped)
                total_processed += 1
                
        # Process and save Val images
        for f in val_files:
            cropped = extract_card(f)
            if cropped is not None:
                save_path = val_out_dir / f"blackfoam_{f.name}"
                cv2.imwrite(str(save_path), cropped)
                total_processed += 1
                
        print(f"[{cls}] Appended: {len(train_files)} to Train | {len(val_files)} to Val")
        
    print(f"\nCompleted! Processed and appended a total of {total_processed} images.")

if __name__ == "__main__":
    append_blackfoam_dataset()