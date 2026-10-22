import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, transforms
from torch.utils.data import DataLoader
import copy
import os
from tqdm import tqdm

torch.manual_seed(42)
torch.backends.cudnn.deterministic = True

class HardwareOptimizedCNN_v3(nn.Module):
    def __init__(self, num_classes=54):
        super().__init__()
        self.features = nn.Sequential(
            # CHANGED: input channels from 3 to 1 for grayscale
            # Replaced MaxPool with stride=2 (64x64 -> 32x32)
            nn.Conv2d(1, 8, kernel_size=3, stride=2, padding=1), 
            nn.BatchNorm2d(8), 
            nn.ReLU(),
            
            # Replaced MaxPool with stride=2 (32x32 -> 16x16)
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

def train_model3():
    # CHANGED: Added Grayscale conversion to the pipeline
    transform = transforms.Compose([
        transforms.Grayscale(num_output_channels=1),
        transforms.ToTensor()
    ])
    
    DATA_DIR = 'scanning_method/final_dataset_64x64'
    train_data = datasets.ImageFolder(os.path.join(DATA_DIR, 'train'), transform=transform)
    val_data = datasets.ImageFolder(os.path.join(DATA_DIR, 'val'), transform=transform)
    
    train_loader = DataLoader(train_data, batch_size=32, shuffle=True, num_workers=4, persistent_workers=True)
    val_loader = DataLoader(val_data, batch_size=32, shuffle=False, num_workers=4, persistent_workers=True)
    
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    
    # CHANGED: Call the v3 model
    model = HardwareOptimizedCNN_v3(num_classes=54).to(device)
    
    criterion = nn.CrossEntropyLoss()
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    
    epochs = 75 
    best_acc = 0.0
    best_model_wts = copy.deepcopy(model.state_dict())
    
    for epoch in range(epochs):
        model.train()
        train_loss, train_corrects = 0.0, 0
        train_bar = tqdm(train_loader, desc=f"Epoch {epoch+1}/{epochs} [Train]")
        
        for images, labels in train_bar:
            images, labels = images.to(device), labels.to(device)
            optimizer.zero_grad()
            outputs = model(images)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()
            
            _, preds = torch.max(outputs, 1)
            train_corrects += torch.sum(preds == labels.data)
            
        epoch_train_acc = train_corrects.double() / len(train_data)
        
        model.eval()
        val_corrects = 0
        
        with torch.no_grad():
            for images, labels in val_loader:
                images, labels = images.to(device), labels.to(device)
                outputs = model(images)
                _, preds = torch.max(outputs, 1)
                val_corrects += torch.sum(preds == labels.data)
                
        epoch_val_acc = val_corrects.double() / len(val_data)
        print(f"Train Acc: {epoch_train_acc:.4f} | Val Acc: {epoch_val_acc:.4f}")
        
        if epoch_val_acc > best_acc:
            best_acc = epoch_val_acc
            best_model_wts = copy.deepcopy(model.state_dict())

    model.load_state_dict(best_model_wts)
    os.makedirs("models", exist_ok=True)
    
    # CHANGED: Save as model3
    save_path = "models/fpga_card_classifier_model3.pth"
    torch.save(model.state_dict(), save_path)
    print(f"\nModel 3 saved to {save_path}")

if __name__ == "__main__":
    train_model3()