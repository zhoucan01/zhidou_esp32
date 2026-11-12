"""Build a self-contained, offline HTML edition of docs/UI操作指南.md."""
import base64
import html
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'docs/UI操作指南.md'
TARGET = ROOT / 'docs/UI操作指南.html'

CSS = '''
:root{color-scheme:light;--ink:#182c35;--muted:#536772;--accent:#21665d;--line:#dce5e4}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:76px}
body{margin:0;background:#f2f5f3;color:var(--ink);font:16px/1.8 "Microsoft YaHei","Segoe UI",sans-serif}
a{color:var(--accent);text-underline-offset:3px}a:hover{color:#b65d2a}
.topbar{position:sticky;top:0;z-index:10;background:#173d38;color:white;padding:12px 26px;display:flex;align-items:center;gap:18px;box-shadow:0 2px 14px #173d3820}
.topbar strong{font-size:18px}.topbar span{color:#c5ded6;font-size:13px;flex:1}
button{font:inherit;cursor:pointer;border:1px solid #7aa69a;background:transparent;color:inherit;border-radius:8px;padding:5px 15px}
.layout{display:grid;grid-template-columns:242px minmax(0,1fr);max-width:1600px;margin:auto}
nav{position:sticky;top:70px;align-self:start;max-height:calc(100vh - 85px);overflow:auto;padding:26px 20px;font-size:13px}
nav .nav-title{letter-spacing:2px;font-size:12px;color:var(--muted);margin-bottom:10px}
nav a{display:block;text-decoration:none;padding:5px 9px;border-radius:5px;line-height:1.6}
nav a:hover{background:#e1ebe6}nav .sub{font-size:12px;padding-left:19px;color:#526c68}
main{min-width:0;padding:30px 32px 70px}h1{font-size:38px;margin:0 0 20px;letter-spacing:1px}
h2{font-size:27px;border-top:2px solid #b5cdc4;padding-top:29px;margin:47px 0 20px}
h3{font-size:20px;margin:25px 0 13px}p{margin:12px 0}strong{font-weight:700}code{font:13px/1.7 Consolas,monospace;background:#e8eeeb;border-radius:4px;padding:2px 5px}
ul,ol{padding-left:26px}li{padding-left:4px;margin:7px 0}blockquote{margin:20px 0;padding:12px 19px;border-left:4px solid var(--accent);background:#e5f0e9}
table{border-collapse:collapse;width:100%;margin:18px 0;font-size:14px;background:white;overflow-wrap:anywhere}
th{background:#e3ece6;text-align:left;font-weight:600}td,th{border:1px solid var(--line);padding:10px 12px;vertical-align:top}tr:nth-child(even) td{background:#f8faf8}
.page-card{margin:28px 0;padding:22px;border:1px solid var(--line);border-radius:16px;background:#fff;box-shadow:0 3px 15px #153d3010;scroll-margin-top:82px}
.page-card h3{margin:0 0 20px;color:#174c43;border-bottom:1px solid var(--line);padding-bottom:13px}
.page-body{display:grid;grid-template-columns:minmax(280px,440px) minmax(0,1fr);gap:26px;align-items:start}
.instructions>:first-child{margin-top:0}.instructions table{font-size:13px}.instructions th,.instructions td{padding:7px 9px}
.gallery{display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}.gallery figure:first-child{grid-column:1/-1}
figure{margin:0 0 14px;min-width:0}figure img{width:100%;height:auto;display:block;border:1px solid #cedbd5;border-radius:9px;cursor:zoom-in}
figcaption{font-size:12px;line-height:1.6;color:var(--muted);margin-top:7px}
.edition{display:inline-block;padding:3px 13px;border-radius:20px;background:#dfece4;color:#22564a;font-size:12px;letter-spacing:1px;margin-bottom:14px}
footer{border-top:1px solid var(--line);margin-top:40px;padding-top:18px;color:var(--muted);font-size:12px}
dialog{padding:16px;max-width:96vw;max-height:96vh;border:0;border-radius:14px;box-shadow:0 10px 60px #0006}dialog::backdrop{background:#102620bc}
dialog img{display:block;width:min(960px,85vw);height:auto;max-height:78vh;object-fit:contain}dialog p{margin:10px 0;font-size:14px}dialog button{float:right;color:#183c33;border-color:#aac3b8}
@media(max-width:1180px){.page-body{grid-template-columns:1fr}.gallery{max-width:680px}.gallery figure:first-child{grid-column:auto}.gallery:has(figure:only-child){grid-template-columns:1fr;max-width:480px}}
@media(max-width:760px){.layout{display:block}nav{position:static;max-height:240px;border-bottom:1px solid var(--line);padding:15px}.topbar{padding:10px 16px}.topbar span{display:none}.topbar strong{flex:1}main{padding:22px 14px}.page-card{padding:16px}h1{font-size:29px}h2{font-size:24px}table{font-size:12px}td,th{padding:7px}.gallery{grid-template-columns:1fr}.gallery figure:first-child{grid-column:auto}}
@media print{body{background:white;font-size:10pt;-webkit-print-color-adjust:exact;print-color-adjust:exact}.topbar,nav,dialog{display:none}.layout{display:block}main{padding:0}h1{font-size:26pt}h2{font-size:19pt;break-before:page}.page-card{box-shadow:none;border:0;border-bottom:1px solid #bbb;padding:12px 0;break-inside:avoid}.page-body{display:block}.gallery{display:flex;flex-wrap:wrap;gap:12px}.gallery figure{width:46%;max-width:360px}.page-card h3{font-size:15pt}a{color:inherit;text-decoration:none}table{font-size:9pt}footer{font-size:8pt}}
'''


def inline(text):
    tokens = []
    def hold(value):
        tokens.append(value)
        return f'\x01{len(tokens)-1}\x02'
    text = re.sub(r'`([^`]+)`', lambda m: hold('<code>' + html.escape(m[1]) + '</code>'), text)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)',
                  lambda m: hold('<a href="' + html.escape(m[2], quote=True) + '">' + html.escape(m[1]) + '</a>'), text)
    text = html.escape(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    return re.sub(r'\x01(\d+)\x02', lambda m: tokens[int(m[1])], text)


def main():
    lines = SOURCE.read_text(encoding='utf-8').splitlines()
    output, nav = [], []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith('<a id='):
            output.append(line)
            i += 1
            continue
        heading = re.match(r'^(#{1,3}) (.+)$', line)
        if heading:
            level, title = len(heading[1]), heading[2]
            page = re.match(r'P(\d+) ', title)
            key = 'p' + page[1] if page else ('guide-title' if level == 1 else f'section-{len(nav)}')
            output.append(f'<h{level} id="{key}">{inline(title)}</h{level}>')
            if level > 1:
                nav.append((key, title, level))
            i += 1
            continue
        picture = re.fullmatch(r'!\[([^\]]*)\]\(([^)]+)\)', line)
        if picture:
            path = (SOURCE.parent / picture[2]).resolve()
            encoded = base64.b64encode(path.read_bytes()).decode('ascii')
            caption = html.escape(picture[1])
            output.append(f'<figure><img src="data:image/png;base64,{encoded}" alt="{caption}" loading="lazy"><figcaption>{caption}</figcaption></figure>')
            i += 1
            continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [cell.strip() for cell in lines[i].strip('|').split('|')]
                if not all(re.fullmatch(r':?-+:?', cell) for cell in cells):
                    rows.append(cells)
                i += 1
            output.append('<table><thead><tr>' + ''.join('<th>' + inline(cell) + '</th>' for cell in rows[0]) + '</tr></thead><tbody>')
            for row in rows[1:]:
                output.append('<tr>' + ''.join('<td>' + inline(cell) + '</td>' for cell in row) + '</tr>')
            output.append('</tbody></table>')
            continue
        bullet = re.match(r'^(- |\d+\. )(.+)$', line)
        if bullet:
            ordered = not line.startswith('- ')
            tag = 'ol' if ordered else 'ul'
            output.append('<' + tag + '>')
            while i < len(lines):
                match = re.match(r'^(\d+\. |' + ('NO_BULLET' if ordered else '- ') + r')(.+)$', lines[i]) if ordered else re.match(r'^- (.+)$', lines[i])
                if not match:
                    break
                output.append('<li>' + inline(match[2] if ordered else match[1]) + '</li>')
                i += 1
            output.append('</' + tag + '>')
            continue
        output.append('<p>' + inline(line) + '</p>')
        i += 1
    body = '\n'.join(output)
    def card(match):
        heading, content = match[1], match[2]
        pictures = re.findall(r'<figure>.*?</figure>', content, flags=re.S)
        rest = re.sub(r'<figure>.*?</figure>', '', content, flags=re.S)
        return '<section class="page-card">' + heading + '<div class="page-body"><div class="gallery">' + ''.join(pictures) + '</div><div class="instructions">' + rest + '</div></div></section>\n'
    body = re.sub(r'(<h3 id="p\d+">.*?</h3>)(.*?)(?=<h[23] |<a id=|$)', card, body, flags=re.S)
    navigation = ''.join(f'<a class="{"sub" if level == 3 else "chapter"}" href="#{key}">{html.escape(title)}</a>' for key, title, level in nav)
    document = '''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UI 操作指南 · 豆腐 / 植物奶 / 清洗</title><style>''' + CSS + '''</style></head><body>
<header class="topbar"><strong>UI 操作指南</strong><span>30 个页面 · 实际页面配图 · 2026-11-09</span><button onclick="window.print()">打印 / 保存 PDF</button></header>
<div class="layout"><nav aria-label="操作指南目录"><div class="nav-title">按功能 / 页面查阅</div>''' + navigation + '''</nav><main>
<span class="edition">首次使用 · 图文版 · 点击图片可放大</span>''' + body + '''
<footer>配图来自当前工程真实 LVGL 主机渲染；并非本次实机拍摄。本文可离线阅读，全部页面图片已经内嵌。</footer>
</main></div><dialog id="zoom"><button id="closeZoom">关闭 ×</button><p id="zoomCaption"></p><img id="zoomImage" alt=""></dialog>
<script>
const zoom=document.getElementById('zoom'),zi=document.getElementById('zoomImage'),zc=document.getElementById('zoomCaption');
document.querySelectorAll('figure img').forEach(img=>img.addEventListener('click',()=>{zi.src=img.src;zi.alt=img.alt;zc.textContent=img.alt;zoom.showModal()}));
document.getElementById('closeZoom').addEventListener('click',()=>zoom.close());
zoom.addEventListener('click',e=>{if(e.target===zoom)zoom.close()});
</script></body></html>'''
    TARGET.write_text(document, encoding='utf-8')
    cards = len(re.findall('class="page-card"', document))
    assert cards == 30, f'Expected 30 page cards, found {cards}'
    print(f'HTML: {TARGET}; page cards={cards}; embedded images={len(re.findall("data:image/png;base64,", document))}')


if __name__ == '__main__':
    main()
