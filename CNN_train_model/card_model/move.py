import os
import shutil
from pathlib import Path

TRAIN_DIR = Path("scanning_method/final_dataset/train")
VAL_DIR = Path("scanning_method/final_dataset/val")
BACKUP_DIR = Path("scanning_method/backup_synthetic")

def move_old_synthetic():
    moved_count = 0
    
    for phase_dir in [TRAIN_DIR, VAL_DIR]:
        if not phase_dir.exists():
            continue
            
        phase_name = phase_dir.name # 'train' or 'val'
        backup_phase_dir = BACKUP_DIR / phase_name
        
        for class_dir in phase_dir.iterdir():
            if not class_dir.is_dir():
                continue
                
            backup_class_dir = backup_phase_dir / class_dir.name
            
            for img_path in class_dir.iterdir():
                filename = img_path.name
                
                # 1. Skip the new, color-corrected images we just generated
                if "_v2_synth_" in filename:
                    continue
                    
                # 2. Skip real camera photos (assuming they are .jpg)
                if img_path.suffix.lower() in ['.jpg', '.jpeg']:
                    continue
                    
                # 3. Move the old synthetic images (.png)
                if img_path.suffix.lower() == '.png':
                    backup_class_dir.mkdir(parents=True, exist_ok=True)
                    shutil.move(str(img_path), str(backup_class_dir / filename))
                    moved_count += 1
                    
    print(f"Extraction complete. Moved {moved_count} old synthetic images to: {BACKUP_DIR}")

if __name__ == "__main__":
    move_old_synthetic()