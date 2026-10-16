"""Build a labeled contact sheet and extract measured GPU-pass timings from evidence.
Offline helper only; requires Pillow, not a game dependency.
"""
from pathlib import Path
import json
import re
from PIL import Image, ImageDraw, ImageFont

HERE=Path(__file__).resolve().parent
result=json.loads((HERE/'RetroPieResults.json').read_text(encoding='utf-8'))
font_path=Path('C:/Windows/Fonts/segoeui.ttf')
font=ImageFont.truetype(str(font_path),24)
small=ImageFont.truetype(str(font_path),20)
canvas=Image.new('RGB',(1312,1212),'#101926')
draw=ImageDraw.Draw(canvas)
modes=['OFF','PS1','N64','PS2','CRT']
first={Path(p).stem.rsplit('_',1)[0] for p in result['screenshots']}
batch=sorted(first)[0]
selected={mode:next(Path(p) for p in result['screenshots'] if Path(p).stem==batch+'_'+mode) for mode in modes}
for i,mode in enumerate(modes):
    x=8+(i%2)*656;y=8+(i//2)*400
    image=Image.open(selected[mode]).convert('RGB')
    image.thumbnail((640,360),Image.Resampling.LANCZOS)
    draw.text((x+4,y+2),mode,font=font,fill='white')
    canvas.paste(image,(x,y+36))
draw.multiline_text((676,880),'UE 5.8.3 | Stage1 copy\n1280 x 720 | same frozen camera\n\nSingle post-process pass\nPS1: 0.030 ms | N64: 0.037 ms\nPS2: 0.037 ms | CRT: 0.027 ms\nRTX 4060 Laptop | D3D12',font=small,fill='#c9d6e8',spacing=6)
canvas.save(HERE/'RetroComparison.png')

summary={'gpu_adapter':'NVIDIA GeForce RTX 4060 Laptop GPU','rhi':'D3D12',
    'pass_measurement':'One profilegpu frame per preset, inclusive PostProcessMaterial duration',
    'pass_ms':{},'images':[]}
mode=None
for line in (HERE/'RetroPIE.log').read_text(encoding='utf-8',errors='replace').splitlines():
    if '[RetroProfile]' in line:mode=line.split('[RetroProfile]')[-1].strip()
    if 'PostProcessMaterial 1280x720 Material=M_RetroCompare' in line:
        values=re.findall(r'([\d.]+) ms',line)
        summary['pass_ms'][mode]=float(values[-1])
summary['pass_ms']['OFF']=0.0
for path in result['screenshots']:
    image=Image.open(path)
    summary['images'].append({'path':path,'size':list(image.size)})
(HERE/'RetroEvidenceSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'pass_ms':summary['pass_ms'],'image_count':len(summary['images'])}))
