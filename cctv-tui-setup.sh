#!/bin/bash

SETUP_DIR="/opt/cctv"

if [ "$EUID" -ne 0 ]; then
    echo "Please run this script as root." >&2
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    echo "Python3 is not installed." >&2
    exit 1
fi

if ! command -v pip3 &> /dev/null; then
    echo "pip is not installed." >&2
    exit 1
fi

if ! python3 -c 'import venv' &> /dev/null; then
    echo "python module 'venv' is not installed." >&2
    exit 1
fi

echo "Installing to $SETUP_DIR"

set -e

if [ -d "$SETUP_DIR" ]; then
	read -p "Directory $SETUP_DIR already exists. Do you want to continue? This may override it's contents. [y/N]: " choice
	case "$choice" in
		y ) echo "Continuing...";;
		* ) echo "Aborting..."; exit 0;;
	esac
else
	mkdir -p "$SETUP_DIR"
fi

set -x
cp cctv/cctv-tui.py "$SETUP_DIR"
cd "$SETUP_DIR"
python3 -m venv --clear .venv
source .venv/bin/activate
pip3 install numpy opencv-python websockets textual

cat > tui <<EOF
#!/bin/bash
cd $SETUP_DIR
source .venv/bin/activate
python3 cctv-tui.py
EOF

chmod +x tui

set +x
echo "Install finished. Try running /opt/cctv/tui"
