"""
Generate QR codes with Grafana grot mascot logos in the center.

Usage:
    cd resources/
    python generate_qr.py

Dependencies:
    pip install qrcode[pil] Pillow opencv-python-headless

Inputs:
    logos/grot-potted-plant.png  - Plant stations QR logo
    logos/grot-thermometer.png   - Venue climate QR logo

Outputs:
    qr/qr-plant-stations-booth.png  - QR linking to the plant stations dashboard tab
    qr/qr-venue-climate.png         - QR linking to the venue climate dashboard tab

Both QR codes point to the GrafanaCon Science Fair IoT dashboard on play.grafana.org
with different tab preselections. The logos are centered on a softened rounded-rectangle
white background. Error correction is set to HIGH so the QR remains scannable despite
the logo covering ~24% of the center.
"""

import cv2
from PIL import Image, ImageDraw
import qrcode


def generate_qr(url, logo_path, output_path):
    logo = Image.open(logo_path).convert("RGBA")
    white_bg = Image.new("RGBA", logo.size, (255, 255, 255, 255))
    logo = Image.alpha_composite(white_bg, logo)

    qr = qrcode.QRCode(
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=10,
        border=4,
    )
    qr.add_data(url)
    qr.make(fit=True)

    qr_img = qr.make_image(fill_color="black", back_color="white").convert("RGBA")
    qr_w, qr_h = qr_img.size

    logo_max = int(qr_w * 0.24)
    logo.thumbnail((logo_max, logo_max), Image.LANCZOS)
    logo_w, logo_h = logo.size

    pad = 14
    bg_w = logo_w + pad * 2
    bg_h = logo_h + pad * 2
    bg = Image.new("RGBA", (bg_w, bg_h), (0, 0, 0, 0))
    draw_bg = ImageDraw.Draw(bg)
    radius = min(bg_w, bg_h) // 10
    draw_bg.rounded_rectangle(
        [(0, 0), (bg_w - 1, bg_h - 1)],
        radius=radius,
        fill=(255, 255, 255, 255),
    )
    bg.paste(logo, (pad, pad), logo)

    paste_x = (qr_w - bg_w) // 2
    paste_y = (qr_h - bg_h) // 2
    qr_img.paste(bg, (paste_x, paste_y), bg)

    qr_img.save(output_path)
    print(f"Saved {output_path} ({qr_img.size})")

    cv_verify = cv2.imread(output_path)
    detector = cv2.QRCodeDetector()
    data, _, _ = detector.detectAndDecode(cv_verify)
    print(f"  Scans OK: {bool(data)}")
    if not data:
        multi = cv2.QRCodeDetectorAruco()
        ret, decoded, _, _ = multi.detectAndDecodeMulti(cv_verify)
        if ret and decoded:
            print(f"  Multi-scan OK: {bool(decoded[0])}")


if __name__ == "__main__":
    generate_qr(
        url=("https://play.grafana.org/d/anxh9qz/grafanacon2026-science-fair-iot"
             "?orgId=1&from=now-3h&to=now&timezone=browser"
             "&dtab=plant-stations-&src=qr-code&cnt=booth&camp=gc-sf-iot"),
        logo_path="logos/grot-potted-plant.png",
        output_path="qr/qr-plant-stations-booth.png",
    )

    generate_qr(
        url=("https://play.grafana.org/d/anxh9qz/grafanacon2026-science-fair-iot"
             "?orgId=1&from=now-3h&to=now&timezone=browser"
             "&dtab=venue-climate&src=qr-code&mdm=print"
             "&cnt=venue-climate-wall-sensor&camp=grafanacon-science-fair-iot"),
        logo_path="logos/grot-thermometer.png",
        output_path="qr/qr-venue-climate.png",
    )
