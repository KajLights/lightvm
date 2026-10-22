"""Independent high-precision reference; no host libm defines expected results."""
from decimal import Decimal, localcontext
from fractions import Fraction
from functools import lru_cache
from math import factorial
import struct

ULPS = {'abs':0,'floor':0,'ceil':0,'trunc':0,'sqrt':0,'min':0,'max':0,
        'sin':4,'cos':4,'log':4,'exp':4,'pow':8}


def value(bits):
    if bits & 0x7f800000 == 0x7f800000: raise ArithmeticError('nonfinite input')
    return Fraction.from_float(struct.unpack('<f', struct.pack('<I',bits))[0])


def binary32(x, negative_zero=False):
    x=Fraction(x);sign=0x80000000 if x<0 or (not x and negative_zero) else 0
    x=abs(x)
    if not x:return sign
    e=x.numerator.bit_length()-x.denominator.bit_length()
    if x < Fraction(2)**e:e-=1
    if e < -126:return sign | round(x*2**149)
    significand=round(x*Fraction(2)**(23-e))
    if significand==2**24:significand//=2;e+=1
    if e>127:raise ArithmeticError('nonfinite result')
    return sign | (e+127)<<23 | (significand-2**23)


@lru_cache(None)
def pi(precision):
    with localcontext() as context:
        context.prec=precision
        total=Decimal(0)
        for k in range(precision//14+2):
            total+=Decimal((-1)**k*factorial(6*k)*(13591409+545140134*k))/Decimal(factorial(3*k)*factorial(k)**3*640320**(3*k))
        return 426880*Decimal(10005).sqrt()/total


def transcendental(name, a, b, precision):
    with localcontext() as context:
        context.prec=precision
        x=Decimal(a.numerator)/Decimal(a.denominator)
        if name=='sqrt':return x.sqrt()
        if name=='log':return x.ln()
        if name in ('exp','pow'):
            if name=='pow':x=x.ln()*(Decimal(b.numerator)/Decimal(b.denominator))
            if x>120:raise ArithmeticError('nonfinite result')
            return Decimal(0) if x < -120 else x.exp()
        circle=2*pi(precision);x=x%circle
        if x>circle/2:x-=circle
        if x<-circle/2:x+=circle
        term=total=x if name=='sin' else Decimal(1)
        power=1 if name=='sin' else 0
        for _ in range(precision+20):
            term*=-(x*x)/((power+1)*(power+2));power+=2;total+=term
            if abs(term)<Decimal(10)**(-precision+5):return total
        raise AssertionError('Taylor series did not converge')


def reference(name, args, precision=90):
    a=value(args[0]);b=value(args[1]) if len(args)>1 else Fraction(0)
    sign=args[0]&0x80000000
    if name=='abs':return args[0]&0x7fffffff
    if name in ('floor','ceil','trunc'):
        n=a.numerator//a.denominator if name=='floor' else -(-a.numerator//a.denominator) if name=='ceil' else int(a)
        return binary32(n,negative_zero=bool(sign))
    if name in ('min','max'):
        if a==b==0:return (args[0]|args[1]) if name=='min' else (args[0]&args[1])
        return args[0] if (a<b)==(name=='min') else args[1]
    if name=='sqrt' and a<0 or name=='log' and a<=0:raise ArithmeticError('domain')
    if name in ('sqrt','sin') and not a:return args[0]
    negative=False
    if name=='pow':
        if not b:return 0x3f800000
        if not a:
            if b<0:raise ArithmeticError('domain')
            return sign if b.denominator==1 and b.numerator%2 else 0
        if a<0:
            if b.denominator!=1:raise ArithmeticError('domain')
            negative=bool(b.numerator%2);a=-a
    result=transcendental(name,a,b,precision)
    return binary32(-result if negative else result,negative_zero=negative)


def stable_reference(name,args):
    values=[]
    for precision in (90,160):
        try:values.append(reference(name,args,precision))
        except ArithmeticError:values.append(None)
    assert values[0]==values[1],(name,args,values)
    return values[0]
