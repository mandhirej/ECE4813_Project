from torchvision import datasets, transforms
from pathlib import Path

DATA_DIR = Path("scanning_method/final_dataset_64x64")
VAL_DIR = DATA_DIR / "val"

dataset = datasets.ImageFolder(
    str(VAL_DIR),
    transform=transforms.ToTensor()
)

print("\nExact class mapping:")
for class_name, index in sorted(
    dataset.class_to_idx.items(),
    key=lambda x: x[1]
):
    print(f"{index:2d} -> {class_name}")