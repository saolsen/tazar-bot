import torch
import torch.nn as nn
import torch.nn.functional as F


class ResidualBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, kernel_size=3, padding=1)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, kernel_size=3, padding=1)
        self.bn2 = nn.BatchNorm2d(channels)

    def forward(self, x):
        residual = x
        out = F.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        out = out + residual  # skip connection
        out = F.relu(out)
        return out


class TazarValueNetwork(nn.Module):
    def __init__(
            self, in_channels=12, base_channels=16, num_res_blocks=6
    ):
        """
        Args:
            in_channels: Number of input planes (12 in your case).
            base_channels: Number of feature maps in the first (and subsequent) convs.
            num_res_blocks: How many residual blocks to stack.
        """
        super().__init__()

        # Initial conv "stem"
        self.conv_in = nn.Conv2d(in_channels, base_channels, kernel_size=3, padding=1)
        self.bn_in = nn.BatchNorm2d(base_channels)

        # Residual tower
        self.res_blocks = nn.ModuleList(
            [ResidualBlock(base_channels) for _ in range(num_res_blocks)]
        )

        # Value head
        # 1x1 conv to reduce channels -> single channel, then we will pool
        self.value_conv = nn.Conv2d(base_channels, 1, kernel_size=1)
        self.bn_value = nn.BatchNorm2d(1)

        # Fully-connected layers to produce a single scalar
        # 16x16 -> we do a global average pool -> shape = (batch, 1, 1, 1) -> flatten -> (batch, 1)
        self.fc1 = nn.Linear(1, 16)  # hidden dimension can be bigger
        self.fc2 = nn.Linear(16, 1)  # final scalar output

    def forward(self, x):
        """
        x: (batch_size, 12, 16, 16)
        Returns: value in [-1, 1].
        """
        # Stem
        x = self.conv_in(x)
        x = self.bn_in(x)
        x = F.relu(x)

        # Residual blocks
        for block in self.res_blocks:
            x = block(x)

        # Value head
        x = self.value_conv(x)  # (batch, 1, 16, 16)
        x = self.bn_value(x)
        x = F.relu(x)

        # Global average pool over the 64x64 dimension
        # shape -> (batch, 1, 1, 1)
        x = F.adaptive_avg_pool2d(x, (1, 1))

        # Flatten to (batch, 1)
        x = torch.flatten(x, start_dim=1)  # shape: (batch, 1)

        # Fully connected
        x = F.relu(self.fc1(x))  # shape: (batch, 32)
        x = self.fc2(x)  # shape: (batch, 1)

        # Output in [-1, 1]
        x = torch.tanh(x)
        return x
