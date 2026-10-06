#!/usr/bin/env python3
'''從 Realtek SDK 的 cmake 檔取出建置需要的片段，印到標準輸出，供 cmake_language(EVAL) 執行。

--region START END：從 START 開始到 END 之前的文字。
--block TOKEN...：以這些字詞（中間可有空白）開頭、到配對右括號為止的一個指令；必須剛好一個。
--within START END：只在 START 到 END 之間找 --block。
--require REGEX：只取內容符合的 --block。
--drop REGEX：刪除片段中符合的整行，可重複。
找不到或不只一個時以結束碼 1 結束，讓 configure 停止。

AP_FLAKE8_CLEAN
'''

import argparse
import re
import sys


def region(text, start, end):
    i = text.find(start)
    if i < 0:
        raise LookupError(f'找不到開頭標記：{start}')
    j = text.find(end, i + len(start))
    if j < 0:
        raise LookupError(f'找不到結尾標記：{end}')
    return text[i:j]


def _close(text, start, tokens):
    '''從 start 的指令開頭取到配對的右括號'''
    k = text.find('(', start)
    depth = 0
    while 0 <= k < len(text):
        c = text[k]
        if c == '"':  # 引號字串：跳到未跳脫的結尾引號，裡面的 # 與括號都不算
            k += 1
            while k < len(text) and text[k] != '"':
                k += 2 if text[k] == '\\' else 1
            k += 1
            continue
        if c == '#':
            nl = text.find('\n', k)
            k = len(text) if nl < 0 else nl
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return text[start:k + 1] + '\n'
        k += 1
    raise LookupError('區塊沒有閉合：' + ' '.join(tokens))


def block(text, tokens, require=None):
    pattern = r'\s*'.join(re.escape(t) for t in tokens)
    if re.match(r'\w', tokens[-1][-1]):
        pattern += r'(?!\w)'
    found = [_close(text, m.start(), tokens) for m in re.finditer(pattern, text)]
    if require is not None:
        found = [b for b in found if re.search(require, b)]
    if len(found) != 1:
        raise LookupError(f'區塊符合 {len(found)} 個（應為 1）：' + ' '.join(tokens))
    return found[0]


def drop(text, patterns):
    regs = [re.compile(p) for p in patterns]
    return ''.join(line for line in text.splitlines(keepends=True)
                   if not any(r.search(line) for r in regs))


def main(argv=None):
    p = argparse.ArgumentParser(description='從 SDK 的 cmake 檔取出片段')
    p.add_argument('file')
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument('--region', nargs=2, metavar=('START', 'END'))
    g.add_argument('--block', nargs='+', metavar='TOKEN')
    p.add_argument('--within', nargs=2, metavar=('START', 'END'))
    p.add_argument('--require')
    p.add_argument('--drop', action='append', default=[])
    a = p.parse_args(argv)
    try:
        with open(a.file, encoding='latin-1') as f:
            text = f.read()
        if a.region:
            out = region(text, *a.region)
        else:
            scope = region(text, *a.within) if a.within else text
            out = block(scope, a.block, a.require)
        sys.stdout.buffer.write(drop(out, a.drop).encode('latin-1'))
    except (OSError, LookupError) as e:
        print(f'sdk_extract: {a.file}: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
