import os
import cv2
import torch
import torch.nn as nn
import numpy as np
from pathlib import Path

TEST_DIR = Path("scanning_method/test")
OUTPUT_DIR = Path("scanning_method/test_model3")
TRAIN_DIR = Path("scanning_method/final_dataset/train")  # Used to get class names

class HardwareOptimizedCNN_v3(nn.Module):
    def __init__(self, num_classes=54):
        super().__init__()
        self.features = nn.Sequential(
            # Stride=2 (64x64 -> 32x32). Note: Input channels changed to 1
            nn.Conv2d(1, 8, kernel_size=3, stride=2, padding=1), 
            nn.BatchNorm2d(8), 
            nn.ReLU(),
            
            # Stride=2 (32x32 -> 16x16)
            nn.Conv2d(8, 16, kernel_size=3, stride=2, padding=1),
            nn.BatchNorm2d(16), 
            nn.ReLU(),
        )
        # Flattened spatial map: 16 channels * 16 width * 16 height = 4096
        self.classifier = nn.Linear(4096, num_classes)

    def forward(self, x):
        x = self.features(x)
        x = torch.flatten(x, 1)
        return self.classifier(x)

def extract_card_with_bbox(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)
    _, thresh = cv2.threshold(blurred, 50, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None, None
    
    largest_contour = max(contours, key=cv2.contourArea)
    x, y, w, h = cv2.boundingRect(largest_contour)
    return img[y:y+h, x:x+w], (x, y, w, h)

def test_pipeline():
    if not TEST_DIR.exists():
        print(f"Error: {TEST_DIR} not found.")
        return
        
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    
    # Dynamically map the 54 classes exactly as PyTorch did during training
    classes = sorted([d.name for d in TRAIN_DIR.iterdir() if d.is_dir()])
    
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = HardwareOptimizedCNN_v3(num_classes=len(classes)).to(device)
    
    # Load Model 3 trained weights
    model_path = "models/fpga_card_classifier_model3.pth"
    if not os.path.exists(model_path):
        print(f"Error: Model weights not found at {model_path}")
        return

    model.load_state_dict(torch.load(model_path, map_location=device))
    model.eval()  # Freeze BatchNorm layers for inference
    
    test_files = list(TEST_DIR.glob("*.*"))
    print(f"Starting inference on {len(test_files)} images using Model 3...")
    
    with torch.no_grad():
        for img_path in test_files:
            original_img = cv2.imread(str(img_path))
            if original_img is None: 
                continue
                
            cropped_img, bbox = extract_card_with_bbox(original_img)
            if cropped_img is None: 
                print(f"Card extraction failed for: {img_path.name}")
                continue
                
            # Preprocess: BGR -> Grayscale, resize to 64x64, normalize to [0, 1]
            gray_crop = cv2.cvtColor(cropped_img, cv2.COLOR_BGR2GRAY)
            resized = cv2.resize(gray_crop, (64, 64), interpolation=cv2.INTER_AREA)
            float_img = resized.astype(np.float32) / 255.0
            
            # Format to PyTorch Tensor: [Batch, Channel, Height, Width] -> [1, 1, 64, 64]
            # Since float_img is 2D (64x64), we use unsqueeze twice to add Batch and Channel dims
            input_tensor = torch.from_numpy(float_img).unsqueeze(0).unsqueeze(0).to(device)
            
            # Inference
            outputs = model(input_tensor)
            predicted_idx = torch.argmax(outputs, dim=1).item()
            prediction_text = classes[predicted_idx]
            
            # Visualization overlay
            x, y, w, h = bbox
            cv2.rectangle(original_img, (x, y), (x+w, y+h), (0, 255, 0), 2)
            cv2.rectangle(original_img, (x, y - 30), (x + max(150, len(prediction_text) * 15), y), (0, 255, 0), -1)
            cv2.putText(original_img, prediction_text, (x + 5, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 2)
            
            save_path = OUTPUT_DIR / f"pred_{img_path.name}"
            cv2.imwrite(str(save_path), original_img)
            print(f"Processed {img_path.name}: {prediction_text}")

if __name__ == "__main__":
    test_pipeline()