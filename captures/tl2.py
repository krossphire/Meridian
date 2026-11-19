import pickle,sys
from tl import names,asc
def events(fn):
    r=pickle.load(open(fn+'.pkl','rb'))
    t0=r[0]['ft']
    ev=[]
    for x in r:
        t=(x['ft']-t0)/1e4
        typ,c,code,dat=x['typ'],x['comp'],x['code'],x['data']
        if typ==1 and code==3 and c:
            b=dat.rstrip(b'\0')
            ev.append((t,'RX',b if b else None,(x['ft']-x['lft'])/1e4))
        elif typ==1 and code==4 and not c:
            ev.append((t,'TX',dat.rstrip(b'\0'),0))
        elif typ==1 and code==4 and c:
            ev.append((t,'TXDONE',None,(x['ft']-x['lft'])/1e4))
        elif typ==3:
            n=names.get(code&0xff,hex(code))
            if n in('GET_COMMSTATUS','WAIT_ON_MASK'): continue
            if (n.startswith('SET') or n in('PURGE','CLR_DTR','CLR_RTS')) and not c: ev.append((t,'IOCTL',n+' '+dat.hex(' '),0))
            elif n.startswith('GET') and c: ev.append((t,'IOCTL',n+' '+dat.hex(' '),0))
        elif typ==2:
            ev.append((t,'OPEN' if code==0x28000019 else 'OPENDONE',None,0))
        elif typ==1 and code==2 and not c:
            ev.append((t,'CLOSE',None,0))
        elif typ==1 and not c and code!=3:
            ev.append((t,'IRP%d'%code,dat.hex(),0))
    return ev
def render(ev,out=sys.stdout,gap=40):
    i=0
    while i<len(ev):
        t,k,b,dur=ev[i]
        if k=='RX':
            buf=[];j=i;last=t
            while j<len(ev) and ev[j][1] in('RX',) and ev[j][0]-last<gap:
                buf.append(ev[j][2]); last=ev[j][0]; j+=1
            bs=b''.join(x if x is not None else b'\x00' for x in buf)
            s=''.join(asc(x) if x is not None else '<00>' for x in buf)
            print('%10.1f RX  [%5.0fms] %s   | %s'%(t,last-t,bs.hex(' '),s),file=out)
            i=j;continue
        if k=='TXDONE': i+=1;continue
        if k=='TX': print('%10.1f TX  %s   | %s'%(t,b.hex(' '),asc(b)),file=out)
        else: print('%10.1f %s %s'%(t,k,b if b else ''),file=out)
        i+=1
if __name__=='__main__':
    render(events(sys.argv[1]))
def condensed(fn,out=sys.stdout):
    import io
    s=io.StringIO(); render(events(fn),s)
    lines=[l for l in s.getvalue().splitlines() if not any(k in l for k in ('GET_','SET_BAUD','SET_LINE','SET_CHARS','SET_HANDFLOW','SET_TIMEOUTS','SET_QUEUE','OPENDONE'))]
    # collapse poll pairs TX R01;/BI1; + RX
    poll=0;start=None
    def flush():
        nonlocal poll,start
        if poll: print('%10s ... %d poll exchanges (R01;/BI1; + replies) collapsed, from %s'%('',poll,start),file=out)
        poll=0
    i=0
    while i<len(lines):
        l=lines[i]
        if ('| R01;' in l or '| BI1;' in l) and i+1<len(lines) and ' RX ' in lines[i+1] and lines[i+1].split('|')[-1].strip() in ('0<01>0230;','289.7;') :
            if not poll: start=l.split()[0]
            poll+=1; i+=2; continue
        flush(); print(l,file=out); i+=1
    flush()
