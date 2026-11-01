#!/usr/bin/env python3
"""Independent exact integer/Fraction oracle, deterministic and dependency-free."""
from fractions import Fraction
from pathlib import Path
from math import gcd
import random
LIMIT=(1<<4096)-1
lines=['# kind\top\ta\tb\texpected']
def emit(kind,op,a,b,value):
    if value=='ERR':out=value
    elif isinstance(value,Fraction):out='ERR' if abs(value.numerator)>LIMIT or value.denominator>LIMIT else f'{value.numerator}/{value.denominator}'
    else:out='ERR' if abs(value)>LIMIT else str(value)
    lines.append('\t'.join(map(str,[kind,op,a,b,out])))
ns=[0,1,2,(1<<32)-1,1<<32,(1<<64)-1,1<<64,1<<127,(1<<128)-1,1<<128,(1<<129)+17,(1<<256)-1,(1<<512)+1,(1<<1024)-1]
rng_nat=random.Random(0xD1A09301)
natural_pairs=[(a,b)for a in ns for b in ns]
natural_pairs += [(rng_nat.getrandbits(rng_nat.choice([33,65,129,257,512,768])),rng_nat.getrandbits(rng_nat.choice([32,64,128,256,512]))or 1)for _ in range(24)]
for a,b in natural_pairs:
 for op in ['add','sub','mul','div','mod','gcd','cmp']:
  v={'add':lambda:a+b,'sub':lambda:a-b if a>=b else 'ERR','mul':lambda:a*b,'div':lambda:a//b if b else 'ERR','mod':lambda:a%b if b else 'ERR','gcd':lambda:gcd(a,b),'cmp':lambda:(a>b)-(a<b)}[op]()
  emit('N',op,a,b,v)
zs=[-(1<<256),-(1<<128),-7,-2,-1,0,1,2,7,(1<<128)-1,(1<<128)+17]
integer_pairs=[(a,b)for a in zs for b in zs]
integer_pairs += [(rng_nat.getrandbits(256)*rng_nat.choice([-1,1]),rng_nat.getrandbits(192)*rng_nat.choice([-1,1])or 1)for _ in range(20)]
for a,b in integer_pairs:
 for op in ['add','sub','mul','div','mod','cmp']:
  if op in ['div','mod'] and b==0:v='ERR'
  elif op=='div':v=(a-(a%abs(b)))//b
  elif op=='mod':v=a%abs(b)
  else:v={'add':lambda:a+b,'sub':lambda:a-b,'mul':lambda:a*b,'cmp':lambda:(a>b)-(a<b)}[op]()
  emit('I',op,a,b,v)
small=[Fraction(n,d)for n in range(-3,4)for d in range(1,5)]
pairs=[(a,b)for a in small for b in small]
rng=random.Random(0xD1A093)
for _ in range(20):
 def q():
  bits=rng.choice([129,192,256,512]);n=rng.getrandbits(bits);d=rng.getrandbits(bits)or 1
  return Fraction(n if rng.randrange(2)else -n,d)
 pairs.append((q(),q()))
pairs += [(Fraction(LIMIT,2),Fraction(LIMIT,2)),(Fraction(LIMIT),Fraction(1)),(Fraction(LIMIT,LIMIT-1),Fraction(-LIMIT,LIMIT-1)),(Fraction(LIMIT,LIMIT-1),Fraction(LIMIT-1,LIMIT))]
for a,b in pairs:
 for op in ['add','sub','mul','div','cmp']:
  try:v={'add':lambda:a+b,'sub':lambda:a-b,'mul':lambda:a*b,'div':lambda:a/b,'cmp':lambda:(a>b)-(a<b)}[op]()
  except ZeroDivisionError:v='ERR'
  emit('Q',op,f'{a.numerator}/{a.denominator}',f'{b.numerator}/{b.denominator}',v)
oracle_path=Path(__file__).resolve().parents[1]/'crates/dlm_core/tests/fixtures/exact093.tsv'
oracle_path.write_text('\n'.join(lines)+'\n');print(len(lines)-1,'exact oracle vectors')

# Matrix oracle: Fraction row reduction and independent Leibniz determinant.
from itertools import permutations
matrix_lines=['# rows\tcols\tinput\trref\tpivots\tdeterminant\tkernel']
def flat(a):return ','.join(f'{x.numerator}/{x.denominator}'for row in a for x in row)
for rows,cols in [(0,0),(0,3),(3,0),(1,3),(3,1),(2,3),(3,2),(3,3),(4,4),(5,5)]:
 for rep in range(5):
  a=[[Fraction(rng.randint(-5,5),rng.randint(1,7))for _ in range(cols)]for _ in range(rows)]
  if rep==4 and rows>1:a[-1]=a[0][:]
  rr=[row[:]for row in a];pivots=[];row=0
  for c in range(cols):
   p=next((r for r in range(row,rows)if rr[r][c]),None)
   if p is None:continue
   rr[row],rr[p]=rr[p],rr[row]
   scale=rr[row][c];rr[row]=[x/scale for x in rr[row]]
   for r in range(rows):
    if r!=row:
     factor=rr[r][c];rr[r]=[x-factor*y for x,y in zip(rr[r],rr[row])]
   pivots.append(c);row+=1
  free=[c for c in range(cols)if c not in pivots];kernel=[[Fraction(0)for _ in free]for _ in range(cols)]
  for j,c in enumerate(free):
   kernel[c][j]=Fraction(1)
   for i,pivot in enumerate(pivots):kernel[pivot][j]=-rr[i][c]
  det='-'
  if rows==cols:
   value=Fraction(0)
   for perm in permutations(range(rows)):
    term=Fraction((-1)**sum(perm[i]>perm[j]for i in range(rows)for j in range(i+1,rows)))
    for r,c in enumerate(perm):term*=a[r][c]
    value+=term
   det=f'{value.numerator}/{value.denominator}'
  matrix_lines.append('\t'.join([str(rows),str(cols),flat(a),flat(rr),','.join(map(str,pivots)),det,flat(kernel)]))
oracle_path.with_name('rational_matrices093.tsv').write_text('\n'.join(matrix_lines)+'\n')
print(len(matrix_lines)-1,'matrix oracle vectors')
