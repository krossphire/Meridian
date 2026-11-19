import pickle,sys,struct
names={0x04:'SET_BAUD',0x08:'SET_QUEUE_SIZE',0x0c:'SET_LINE_CONTROL',0x1c:'SET_TIMEOUTS',0x20:'GET_TIMEOUTS',0x24:'SET_DTR',0x28:'CLR_DTR',0x30:'SET_RTS',0x34:'CLR_RTS',0x44:'SET_WAIT_MASK',0x48:'WAIT_ON_MASK',0x4c:'PURGE',0x50:'GET_BAUD',0x54:'GET_LINE_CONTROL',0x58:'GET_CHARS',0x5c:'SET_CHARS',0x60:'GET_HANDFLOW',0x64:'SET_HANDFLOW',0x68:'GET_MODEMSTATUS',0x6c:'GET_COMMSTATUS',0x74:'GET_PROPERTIES'}
def asc(b): return ''.join(chr(c) if 32<=c<127 else '<%02x>'%c for c in b)
def strip(b):
    return b.rstrip(b'\0')
def show(fn,quiet=True,out=sys.stdout):
    r=pickle.load(open(fn+'.pkl','rb'))
    t0=r[0]['ft']
    for x in r:
        t=(x['ft']-t0)/1e4
        typ,c,code=x['typ'],x['comp'],x['code']
        dat=x['data']
        if typ==3 and (code&0xff) in (0x6c,0x48) and quiet: continue
        if typ==1 and code==3 and not c and quiet: continue
        if typ==1 and code==3:
            b=strip(dat)
            if not b and quiet: continue
            print('%10.1f RX  %-40s %s'%(t,b.hex(' '),asc(b)),file=out);continue
        if typ==1 and code==4:
            if c: continue
            b=strip(dat)
            print('%10.1f TX  %-40s %s'%(t,b.hex(' '),asc(b)),file=out);continue
        if typ==3:
            n=names.get(code&0xff,hex(code))
            setter=n.startswith('SET') or n in('PURGE','CLR_DTR','CLR_RTS')
            if setter and c: continue
            if n.startswith('GET') and not c: continue
            print('%10.1f %s %s %s'%(t,'IOCTL' ,n,dat.hex(' ')),file=out);continue
        print('%10.1f type%d comp%d code=%x %s'%(t,typ,c,code,dat.hex(' ')),file=out)
if __name__=='__main__':
    show(sys.argv[1], quiet=len(sys.argv)<3)
