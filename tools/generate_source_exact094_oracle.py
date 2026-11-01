#!/usr/bin/env python3
"""Independent oracle for the ordinary DLM exact arithmetic source library."""
from fractions import Fraction
from pathlib import Path
from math import gcd
import random
rng=random.Random(0xD1A094)
lines=['# kind\top\ta\tb\texpected']
pairs=[(0,0),(65535,1),(65536,-65535),(-(1<<128)-1,65537),((1<<160)+31,-(1<<64)-3)]
pairs += [(rng.getrandbits(rng.choice([16,32,65,129,161]))*rng.choice([-1,1]),(rng.getrandbits(rng.choice([16,32,65]))or 1)*rng.choice([-1,1]))for _ in range(10)]
for a,b in pairs:
 for op in ['add','sub','mul','cmp','gcd','div']:
  if op=='div':
   if not b:continue
   rem=a%abs(b);v=f'{(a-rem)//b},{rem}'
  else:v={'add':lambda:a+b,'sub':lambda:a-b,'mul':lambda:a*b,'cmp':lambda:(a>b)-(a<b),'gcd':lambda:gcd(a,b)}[op]()
  lines.append('\t'.join(map(str,['I',op,a,b,v])))
pairs=[(Fraction(0),Fraction(-1,3)),(Fraction(65537,3),Fraction(-65535,7)),(Fraction((1<<160)+1,31),Fraction((1<<129)+7,17))]
pairs += [(Fraction(rng.randrange(-10**12,10**12),rng.randrange(1,10**10)),Fraction(rng.randrange(-10**12,10**12),rng.randrange(1,10**10)))for _ in range(4)]
for a,b in pairs:
 for op in ['add','sub','mul','div','cmp']:
  if op=='div'and b==0:continue
  v={'add':lambda:a+b,'sub':lambda:a-b,'mul':lambda:a*b,'div':lambda:a/b,'cmp':lambda:(a>b)-(a<b)}[op]()
  out=str(v)if op=='cmp'else f'{v.numerator}/{v.denominator}'
  lines.append('\t'.join(map(str,['Q',op,f'{a.numerator}/{a.denominator}',f'{b.numerator}/{b.denominator}',out])))
p=Path(__file__).resolve().parents[1]/'crates/dlm_core/tests/fixtures/source_exact094.tsv'
p.write_text('\n'.join(lines)+'\n');print(len(lines)-1,'independent source oracle vectors')
