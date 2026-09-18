#!/bin/bash
# Generate the test image folders used by run-test.sh (needs ImageMagick 7).
#   images/          mixed content: aspect ratios, alpha, EXIF rotation, a huge
#                    photo, a corrupt file, non-images and a subfolder
#   images-formats/  pixel format variants of PNG, BMP, JPEG, TIFF and ICO
#   images-quality/  downscaling quality checks
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

rm -rf images images-formats images-quality
mkdir -p images/Subfolder images-formats images-quality

# images/
magick -size 1920x1080 gradient:'#1e3c72'-'#2a5298' -gravity center -pointsize 120 -fill white \
    -annotate 0 'Wide 16:9' images/wide-screenshot.png
magick -size 900x1600 gradient:'#f7971e'-'#ffd200' -gravity center -pointsize 110 -fill black \
    -annotate 0 'Tall' images/tall-portrait.png
magick -size 40x30 xc:'#e74c3c' images/tiny-40x30.png
magick -size 512x512 xc:none -fill '#27ae60' -draw 'circle 256,256 256,40' -fill white -pointsize 90 \
    -gravity center -annotate 0 'alpha' images/alpha-circle.png
magick wizard: -resize 300% images/wizard.jpg
magick logo: images/logo.gif
magick rose: -resize 400% images/rose.bmp
magick -seed 1 -size 6000x4000 plasma:fractal -blur 0x2 -quality 90 images/big-photo-6000x4000.jpg
magick -size 800x800 xc:white -fill black -pointsize 30 \
    -annotate +20+60 'Text screenshot: lots of small text\nline 2\nline 3' images/text-screenshot.png
magick -size 1200x600 gradient:'#8e44ad'-'#3498db' -gravity north -pointsize 100 -fill white \
    -annotate +0+40 'EXIF rot 90' images/exif-rotated.jpg
python3 - images/exif-rotated.jpg <<'EOF'
# add an EXIF block with Orientation = 6 (rotate 90 degrees clockwise to display)
import struct, sys
path = sys.argv[1]
data = open(path, 'rb').read()
tiff = b'II*\0' + struct.pack('<IH', 8, 1) + struct.pack('<HHIHH', 0x0112, 3, 1, 6, 0) + struct.pack('<I', 0)
payload = b'Exif\0\0' + tiff
app1 = b'\xff\xe1' + struct.pack('>H', len(payload) + 2) + payload
pos = 4 + struct.unpack('>H', data[4:6])[0] if data[2:4] == b'\xff\xe0' else 2
open(path, 'wb').write(data[:pos] + app1 + data[pos:])
EOF
python3 -c "import random; random.seed(1); open('images/corrupt.png', 'wb').write(bytes(random.getrandbits(8) for _ in range(5000)))"
printf 'hello\n' > images/notes.txt
[ -f thumbtest.exe ] && cp thumbtest.exe images/program.exe
cp images/rose.bmp images/Subfolder/inner-rose.bmp
cp images/wizard.jpg images/Subfolder/inner-wizard.jpg

# images-formats/
mk() { magick -size 400x300 gradient:red-blue -fill white -pointsize 60 -gravity center -annotate 0 TEST "$@"; }
mk -depth 8 -type TrueColor PNG24:images-formats/rgb8.png
mk -depth 8 -alpha set -type TrueColorAlpha PNG32:images-formats/rgba8.png
mk -depth 16 -type TrueColor PNG48:images-formats/rgb16.png
mk -depth 16 -alpha set PNG64:images-formats/rgba16.png
mk -colorspace gray -depth 8 -type Grayscale images-formats/gray8.png
mk -colorspace gray -alpha set -depth 8 -type GrayscaleAlpha images-formats/grayalpha8.png
mk -colorspace gray -alpha set -depth 16 -type GrayscaleAlpha images-formats/grayalpha16.png
mk -colors 16 PNG8:images-formats/palette.png
mk images-formats/photo.jpg
mk -type TrueColor BMP3:images-formats/bmp24.bmp
mk -alpha set BMP:images-formats/bmp32.bmp
mk images-formats/tiff.tif
mk -resize 256x256! images-formats/icon.ico
magick -size 400x300 xc:none -fill '#e67e22' -draw 'circle 200,150 200,20' -depth 8 \
    PNG32:images-formats/semi-transparent.png

# images-quality/
cp images/text-screenshot.png images/wide-screenshot.png images/big-photo-6000x4000.jpg \
    images/tall-portrait.png images-quality/
magick -size 1920x1080 xc:white -fill black -pointsize 28 \
    -annotate +40+80 'Lv.35 [ABC]PlayerName - Share [Warzone #1 X:123 Y:456]' \
    -fill '#1e5aa8' -draw 'rectangle 40,120 900,520' -fill white -pointsize 64 -annotate +80+330 'Chat window' \
    -stroke black -strokewidth 3 -draw 'line 0,700 1920,1000' images-quality/chat-like.png

echo "test images generated"
