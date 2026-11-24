#!/usr/bin/env python3
"""Build the engine's English text file from the fan translation disc (ArcaneAria, v1.0).

The patched mes.scp keeps the game's format (Shift-JIS, one message per CR-separated line) with the
text in capitals, as the 3DO font had no lower case. The engine renders text with a TrueType font, so
this converts it to sentence case and writes UTF-8, one message per line (line n = MESSAGE n):

    make_text_en.py <extracted-en/fs/OrgData/mes/mes.scp> <engine/text/mes_en.txt>

Messages 400+ live in launchme, which the patch left untouched (they stayed Japanese); they are
translated here. The output is derived from the user's disc and is not stored in the repository."""
import re
import sys

NAMES = r"\b(hauzer|ellen|god)\b"
EXE_TEXT = {400: "This key doesn't seem to work here.", 401: "There's no point using it here."}


def sentence_case(line, continues):
    s = line.lower()
    # "I" and its contractions, names
    s = re.sub(r"\bi\b", "I", s)
    s = re.sub(NAMES, lambda m: m.group(1).capitalize(), s)
    s = re.sub(r"\bdr\.", "Dr.", s)
    # capitals at the start and after . ! ? - but not after an ellipsis inside a sentence
    out, cap = [], not continues
    for i, ch in enumerate(s):
        if cap and ch.isalpha():
            out.append(ch.upper())
            cap = False
            continue
        out.append(ch)
        if ch in '!?':
            cap = True
        elif ch == '.':
            ellipsis = s[max(0, i - 2):i + 1] == '...' or s[i:i + 3] == '...' or s[max(0, i - 1):i + 2] == '...'
            if not ellipsis and not s[max(0, i - 2):i + 1].endswith('Dr.'):
                cap = True
            elif s.startswith('...') and i < 3 and not continues:
                cap = True       # a line that opens with "..." still starts a sentence
        elif ch.isalnum():
            cap = False
    s = ''.join(out)
    # stutters keep the second part lower case: "I-It" -> "I-it", "D-Dr." stays
    s = re.sub(r"\b([A-Z])-([A-Z])([a-z])", lambda m: f"{m.group(1)}-{m.group(2).lower()}{m.group(3)}", s)
    s = re.sub(r"\b(D)-(d)r\.", r"\1-Dr.", s)
    return s


def main(src, dst):
    lines = open(src, 'rb').read().split(b'\r')
    out, prev = [], ''
    for i, raw in enumerate(lines):
        t = raw.decode('cp932', 'replace').replace('\u3000', ' ')
        t = ''.join(c for c in t if c >= ' ').strip()       # drop control bytes (line 251 has 0x03s)
        if i == 0 or t in ('', '='):
            out.append('')
            prev = ''
            continue
        continues = bool(prev) and prev[-1] not in '.!?"…' and not prev.endswith('...')
        out.append(sentence_case(t, continues))
        prev = t
    while len(out) < 400:
        out.append('')
    for k in sorted(EXE_TEXT):
        while len(out) <= k:
            out.append('')
        out[k] = EXE_TEXT[k]
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print(f'{dst}: {len(out)} lines')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
