import struct,sys,pickle
def parse(fn):
    d=open(fn,'rb').read()
    # find start: first record with kind 1 and inner len match after 0x13000
    o=0x13000
    while True:
        l,k=struct.unpack_from('<II',d,o)
        if k==1 and 0x28<=l<0x2000 and struct.unpack_from('<I',d,o+24)[0]==l: break
        o+=1
    recs=[]
    while o+8<=len(d):
        ln,kind=struct.unpack_from('<II',d,o)
        if kind!=1 or ln<0x28 or ln>0x10000 or o+8+ln>len(d):
            n=o+1;found=False
            while n+28<len(d):
                l2,k2=struct.unpack_from('<II',d,n)
                if k2==1 and 0x28<=l2<0x2000 and struct.unpack_from('<I',d,n+24)[0]==l2:
                    found=True;break
                n+=1
            if not found: break
            o=n;continue
        p=o+8
        seq=struct.unpack_from('<Q',d,p)[0]
        ft=struct.unpack_from('<Q',d,p+8)[0]
        il,ity=struct.unpack_from('<II',d,p+16)
        lft=struct.unpack_from('<Q',d,p+24)[0]
        st=struct.unpack_from('<I',d,p+32)[0]
        fl=d[p+36]
        code=struct.unpack_from('<I',d,p+37)[0]
        data=d[p+41:o+8+ln]
        recs.append(dict(off=o,seq=seq&0x00ffffffffffffff,sf=seq>>56,ft=ft,ilen=il,typ=ity&0x7fffffff,comp=ity>>31,lft=lft,st=st,fl=fl,code=code,data=data,raw=d[p+16:o+8+ln]))
        o+=8+ln
    return recs
if __name__=='__main__':
    for fn in sys.argv[1:]:
        r=parse(fn)
        seqs=[x['seq'] for x in r]
        gaps=[(a,b) for a,b in zip(seqs,seqs[1:]) if b!=a+1]
        print(fn,len(r),'first seq',seqs[0],'last',seqs[-1],'gaps',gaps[:10])
        pickle.dump(r,open(fn+'.pkl','wb'))
