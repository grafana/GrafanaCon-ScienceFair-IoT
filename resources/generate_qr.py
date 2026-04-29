"""
Generate QR codes, optionally with a logo embedded in the center.

Usage (single QR):
    python generate_qr.py --url https://example.com --output out.png
    python generate_qr.py --url https://example.com --output out.png --logo logo.png

Usage (batch via JSON config):
    python generate_qr.py --config qr-batch.json

    Where qr-batch.json is a JSON array of objects, e.g.:
    [
      {
        "url": "https://example.com/a",
        "output": "qr/a.png",
        "logo": "logos/a.png"
      },
      {
        "url": "https://example.com/b",
        "output": "qr/b.png"
      }
    ]

Optional knobs (CLI flags or per-entry keys in the config file):
    --logo-ratio   Fraction of QR width the logo occupies (default 0.24)
    --padding      White padding around the logo, in pixels (default 14)
    --box-size     Size of each QR module in pixels (default 10)
    --border       QR quiet-zone border, in modules (default 4)
    --no-verify    Skip the OpenCV scan-back verification step

Dependencies:
    pip install "qrcode[pil]" Pillow opencv-python-headless

Notes:
    Error correction is set to HIGH so the QR remains scannable despite a logo
    covering up to ~30% of the center. Logos are placed on a softened
    rounded-rectangle white background for contrast.
"""

import argparse
import json
import os
import sys
from typing import Optional

import cv2
from PIL import Image, ImageDraw
import qrcode


def generate_qr(
    url: str,
    output_path: str,
    logo_path: Optional[str] = None,
    logo_ratio: float = 0.24,
    padding: int = 14,
    box_size: int = 10,
    border: int = 4,
    verify: bool = True,
) -> None:
    qr = qrcode.QRCode(
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=box_size,
        border=border,
    )
    qr.add_data(url)
    qr.make(fit=True)

    qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGBA")

    if logo_path:
        logo = Image.open(logo_path).convert("RGBA")
        white_bg = Image.new("RGBA", logo.size, (255, 255, 255, 255))
        logo = Image.alpha_composite(white_bg, logo)

        qr_w, qr_h = qr_img.size
        logo_max = int(qr_w * logo_ratio)
        logo.thumbnail((logo_max, logo_max), Image.LANCZOS)
        logo_w, logo_h = logo.size

        bg_w = logo_w + padding * 2
        bg_h = logo_h + padding * 2
        bg = Image.new("RGBA", (bg_w, bg_h), (0, 0, 0, 0))
        draw_bg = ImageDraw.Draw(bg)
        radius = min(bg_w, bg_h) // 10
        draw_bg.rounded_rectangle(
            [(0, 0), (bg_w - 1, bg_h - 1)],
            radius=radius,
            fill=(255, 255, 255, 255),
        )
        bg.paste(logo, (padding, padding), logo)

        paste_x = (qr_w - bg_w) // 2
        paste_y = (qr_h - bg_h) // 2
        qr_img.paste(bg, (paste_x, paste_y), bg)

    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    qr_img.save(output_path)
    print(f"Saved {output_path} ({qr_img.size})")

    if verify:
        cv_verify = cv2.imread(output_path)
        detector = cv2.QRCodeDetector()
        data, _, _ = detector.detectAndDecode(cv_verify)
        print(f"  Scans OK: {bool(data)}")
        if not data:
            multi = cv2.QRCodeDetectorAruco()
            ret, decoded, _, _ = multi.detectAndDecodeMulti(cv_verify)
            if ret and decoded:
                print(f"  Multi-scan OK: {bool(decoded[0])}")


def _run_from_config(config_path: str, defaults: dict) -> None:
    with open(config_path, "r", encoding="utf-8") as f:
        entries = json.load(f)

    if not isinstance(entries, list):
        raise ValueError("Config file must contain a JSON array of QR entries.")

    for i, entry in enumerate(entries):
        if "url" not in entry or "output" not in entry:
            raise ValueError(f"Entry {i} is missing required 'url' or 'output'.")

        generate_qr(
            url=entry["url"],
            output_path=entry["output"],
            logo_path=entry.get("logo"),
            logo_ratio=entry.get("logo_ratio", defaults["logo_ratio"]),
            padding=entry.get("padding", defaults["padding"]),
            box_size=entry.get("box_size", defaults["box_size"]),
            border=entry.get("border", defaults["border"]),
            verify=entry.get("verify", defaults["verify"]),
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate QR codes with an optional centered logo.",
    )
    parser.add_argument("--url", help="URL or text to encode in the QR code.")
    parser.add_argument("--output", help="Output PNG path for the QR code.")
    parser.add_argument("--logo", help="Optional path to a logo image (PNG with alpha works best).")
    parser.add_argument(
        "--config",
        help="Path to a JSON file describing a batch of QR codes to generate.",
    )
    parser.add_argument("--logo-ratio", type=float, default=0.24,
                        help="Fraction of QR width the logo occupies (default: 0.24).")
    parser.add_argument("--padding", type=int, default=14,
                        help="White padding (px) around the logo (default: 14).")
    parser.add_argument("--box-size", type=int, default=10,
                        help="Size of each QR module in pixels (default: 10).")
    parser.add_argument("--border", type=int, default=4,
                        help="QR quiet-zone border, in modules (default: 4).")
    parser.add_argument("--no-verify", action="store_true",
                        help="Skip OpenCV scan-back verification.")

    args = parser.parse_args()

    defaults = {
        "logo_ratio": args.logo_ratio,
        "padding": args.padding,
        "box_size": args.box_size,
        "border": args.border,
        "verify": not args.no_verify,
    }

    if args.config:
        _run_from_config(args.config, defaults)
        return 0

    if not args.url or not args.output:
        parser.error("Either --config, or both --url and --output, must be provided.")

    generate_qr(
        url=args.url,
        output_path=args.output,
        logo_path=args.logo,
        logo_ratio=args.logo_ratio,
        padding=args.padding,
        box_size=args.box_size,
        border=args.border,
        verify=not args.no_verify,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
