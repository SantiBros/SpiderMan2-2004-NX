# switch_screen.py <in.tga> <out.png>: a game frame placed on the Switch's 1280x720 screen as switch/screen.cpp does
# (height fitted to 720, shape kept, black bars at the sides, nearest-neighbour scaling)
import sys
from PIL import Image
src = Image.open(sys.argv[1]).convert('RGB')
w, h = src.size
oh, ow = 720, min(1280, w * 720 // h)
scr = Image.new('RGB', (1280, 720), (0, 0, 0))
scr.paste(src.resize((ow, oh), Image.NEAREST), ((1280 - ow) // 2, 0))
scr.save(sys.argv[2])
