#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, socket, struct, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Literal

ACTION_PUSH=0x01
ACTION_POLL=0x02
ACTION_AUTH_CHALLENGE=0x04
ACTION_AUTH_RESPONSE=0x05
MAILBOX_SIZE=16
FRAME_HEADER_SIZE=21
CHALLENGE_SIZE=32
MAX_PAYLOAD_SIZE=1024*1024
ZERO_MAILBOX=b"\x00"*MAILBOX_SIZE
ProbeStatus=Literal["accepted","rejected","indeterminate"]

class LoadGenError(RuntimeError): pass
class ProtocolError(LoadGenError): pass
class SocksError(LoadGenError): pass

@dataclass
class ProbeRecord:
    round_no:int
    status:ProbeStatus
    detail:str

@dataclass
class Summary:
    onion:str
    clients_requested:int
    clients_connected:int=0
    payload_size:int=0
    rounds_completed:int=0
    messages_sent:int=0
    payload_bytes_sent:int=0
    wire_bytes_sent:int=0
    rejection_detected:bool=False
    liveness_ok:bool=False
    sink_mailbox_hex:str=""
    duration_seconds:float=0.0
    probes:list[ProbeRecord]=field(default_factory=list)

def recv_exact(sock,size):
    data=bytearray()
    while len(data)<size:
        chunk=sock.recv(size-len(data))
        if not chunk:
            raise ConnectionError(f"connection closed while receiving {size} bytes ({len(data)} bytes received)")
        data.extend(chunk)
    return bytes(data)

def build_frame(action,mailbox_id,payload=b""):
    if len(mailbox_id)!=MAILBOX_SIZE: raise ValueError("bad mailbox size")
    if len(payload)>MAX_PAYLOAD_SIZE: raise ValueError("payload too large")
    return bytes([action])+mailbox_id+struct.pack("<I",len(payload))+payload

def send_frame(sock,action,mailbox_id,payload=b""):
    sock.sendall(build_frame(action,mailbox_id,payload))

def recv_frame(sock):
    header=recv_exact(sock,FRAME_HEADER_SIZE)
    action=header[0]
    mailbox=header[1:17]
    length=struct.unpack("<I",header[17:21])[0]
    if length>MAX_PAYLOAD_SIZE: raise ProtocolError("oversized payload")
    payload=recv_exact(sock,length) if length else b""
    return action,mailbox,payload

def socks5_connect(socks_host,socks_port,onion,target_port,timeout):
    onion_bytes=onion.encode("ascii")
    if not onion_bytes or len(onion_bytes)>255: raise SocksError("bad domain length")
    sock=socket.create_connection((socks_host,socks_port),timeout=timeout)
    sock.settimeout(timeout)
    try:
        sock.sendall(b"\x05\x01\x00")
        if recv_exact(sock,2)!=b"\x05\x00": raise SocksError("no-auth rejected")
        req=b"\x05\x01\x00\x03"+bytes([len(onion_bytes)])+onion_bytes+struct.pack(">H",target_port)
        sock.sendall(req)
        h=recv_exact(sock,4)
        ver,rep,rsv,atyp=h
        if ver!=5 or rsv!=0: raise SocksError("invalid response")
        if rep!=0: raise SocksError(f"CONNECT failed 0x{rep:02x}")
        if atyp==1: recv_exact(sock,4)
        elif atyp==3: recv_exact(sock,recv_exact(sock,1)[0])
        elif atyp==4: recv_exact(sock,16)
        else: raise SocksError("unsupported ATYP")
        recv_exact(sock,2)
        return sock
    except Exception:
        sock.close()
        raise

def solve_pow(challenge):
    if len(challenge)!=CHALLENGE_SIZE: raise ProtocolError("bad challenge size")
    for nonce in range(1<<64):
        nonce_bytes=struct.pack("<Q",nonce)
        digest=hashlib.blake2b(challenge+nonce_bytes,digest_size=16).digest()
        if digest[0]==0 and digest[1]<0x10:
            return nonce_bytes
    raise LoadGenError("no nonce found")

def authenticate(sock):
    action,_,challenge=recv_frame(sock)
    if action!=ACTION_AUTH_CHALLENGE: raise ProtocolError("expected challenge")
    nonce=solve_pow(challenge)
    send_frame(sock,ACTION_AUTH_RESPONSE,ZERO_MAILBOX,nonce)
    mailbox=os.urandom(MAILBOX_SIZE)
    send_frame(sock,ACTION_POLL,mailbox)
    a,m,p=recv_frame(sock)
    if a!=ACTION_POLL or m!=mailbox or p:
        raise ProtocolError("auth validation failed")

def create_authenticated_client(args):
    sock=socks5_connect(args.socks_host,args.socks_port,args.onion,args.target_port,args.timeout)
    try:
        authenticate(sock)
        return sock
    except Exception:
        sock.close()
        raise

def probe_connection(args):
    try:
        sock=socks5_connect(args.socks_host,args.socks_port,args.onion,args.target_port,args.probe_timeout)
    except (OSError,SocksError,ConnectionError) as exc:
        return "indeterminate",f"SOCKS/Tor error: {exc}"
    try:
        action,_,payload=recv_frame(sock)
        if action==ACTION_AUTH_CHALLENGE and len(payload)==CHALLENGE_SIZE:
            return "accepted","AUTH_CHALLENGE received"
        return "indeterminate",f"unexpected first frame action=0x{action:02x}"
    except (ConnectionResetError,BrokenPipeError,ConnectionError) as exc:
        return "rejected",f"closed before AUTH_CHALLENGE: {exc}"
    except socket.timeout:
        return "indeterminate","timeout waiting for AUTH_CHALLENGE"
    except OSError as exc:
        return "indeterminate",f"socket error: {exc}"
    finally:
        sock.close()

def send_parallel_round(pool,clients,frame):
    futures=[pool.submit(c.sendall,frame) for c in clients]
    for f in as_completed(futures): f.result()

def verify_existing_session(sock):
    mailbox=os.urandom(MAILBOX_SIZE)
    try:
        send_frame(sock,ACTION_POLL,mailbox)
        a,m,p=recv_frame(sock)
        return a==ACTION_POLL and m==mailbox and p==b""
    except (OSError,ConnectionError,ProtocolError):
        return False

def write_summary(path,summary):
    if not path: return
    out=Path(path); out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(asdict(summary),indent=2)+"\n",encoding="utf-8")

def parse_args():
    p=argparse.ArgumentParser()
    p.add_argument("--onion",required=True)
    p.add_argument("--socks-host",default="127.0.0.1")
    p.add_argument("--socks-port",type=int,default=9050)
    p.add_argument("--target-port",type=int,default=80)
    p.add_argument("--clients",type=int,default=100)
    p.add_argument("--payload-size",type=int,default=MAX_PAYLOAD_SIZE)
    p.add_argument("--max-sent-mib",type=int,default=1800)
    p.add_argument("--monitor-delay",type=float,default=1.25)
    p.add_argument("--rejection-confirmations",type=int,default=2)
    p.add_argument("--post-rejection-hold",type=float,default=5.0)
    p.add_argument("--timeout",type=float,default=30.0)
    p.add_argument("--probe-timeout",type=float,default=10.0)
    p.add_argument("--connect-retries",type=int,default=5,
                   help="maximum attempts for each authenticated client connection")
    p.add_argument("--retry-delay",type=float,default=1.0,
                   help="delay in seconds between transient connection retries")
    p.add_argument("--summary-json")
    p.add_argument(
        "--setup-only",
        action="store_true",
        help="establish/authenticate clients and run a baseline probe without generating load",
    )
    a=p.parse_args()
    if a.clients<=0:p.error("--clients must be > 0")
    if not 1<=a.payload_size<=MAX_PAYLOAD_SIZE:p.error("--payload-size out of range")
    if a.max_sent_mib<=0:p.error("--max-sent-mib must be > 0")
    if a.rejection_confirmations<=0:p.error("--rejection-confirmations must be > 0")
    if a.connect_retries<=0:p.error("--connect-retries must be > 0")
    if a.retry_delay<0:p.error("--retry-delay must be >= 0")
    if "://" in a.onion:p.error("--onion must be hostname")
    return a


def establish_authenticated_clients(args,summary):
    clients=[]
    for client_no in range(1,args.clients+1):
        last_exc=None
        for attempt in range(1,args.connect_retries+1):
            try:
                sock=create_authenticated_client(args)
                clients.append(sock)
                summary.clients_connected=len(clients)
                if len(clients)%10==0 or len(clients)==args.clients:
                    print(f"[+] Authenticated clients: {len(clients)}/{args.clients}")
                break
            except (socket.timeout,OSError,SocksError,ConnectionError) as exc:
                last_exc=exc
                print(
                    f"[!] Client {client_no}/{args.clients} connection attempt "
                    f"{attempt}/{args.connect_retries} failed: {exc}",
                    file=sys.stderr,
                )
                if attempt<args.connect_retries and args.retry_delay:
                    time.sleep(args.retry_delay)
        else:
            for sock in clients:
                try:sock.shutdown(socket.SHUT_RDWR)
                except OSError:pass
                sock.close()
            raise LoadGenError(
                f"client {client_no}/{args.clients} could not authenticate after "
                f"{args.connect_retries} attempts: {last_exc}"
            )
    return clients


def baseline_probe_with_retries(args,summary):
    last_status="indeterminate"
    last_detail="no probe attempted"
    for attempt in range(1,args.connect_retries+1):
        status,detail=probe_connection(args)
        last_status,last_detail=status,detail
        summary.probes.append(ProbeRecord(0,status,detail))
        print(
            f"[i] Baseline probe attempt {attempt}/{args.connect_retries}: "
            f"{status} ({detail})"
        )
        if status=="accepted":
            return
        if status=="rejected":
            raise LoadGenError("baseline probe was rejected before load generation")
        if attempt<args.connect_retries and args.retry_delay:
            time.sleep(args.retry_delay)
    raise LoadGenError(
        f"baseline probe did not become accepted after {args.connect_retries} attempts: "
        f"{last_status} ({last_detail})"
    )


def main():
    args=parse_args()
    clients=[]
    sink=os.urandom(MAILBOX_SIZE)
    payload=b"\xA5"*args.payload_size
    push_frame=build_frame(ACTION_PUSH,sink,payload)
    summary=Summary(args.onion,args.clients,payload_size=args.payload_size,sink_mailbox_hex=sink.hex())
    max_sent_bytes=args.max_sent_mib*1024*1024
    started=time.monotonic()
    try:
        print(f"[i] Establishing {args.clients} authenticated connections")
        clients=establish_authenticated_clients(args,summary)
        # Let failed/retried Tor streams propagate their close before measurement.
        time.sleep(1.0)
        baseline_probe_with_retries(args,summary)
        if args.setup_only:
            summary.liveness_ok=verify_existing_session(clients[0])
            if not summary.liveness_ok:
                print("[!] Setup succeeded, but existing session liveness check failed",file=sys.stderr)
                return 3
            print("[+] Setup-only check passed: SOCKS5, onion routing, PoW auth and POLL are working")
            return 0
        consecutive=0
        with ThreadPoolExecutor(max_workers=args.clients) as pool:
            while summary.payload_bytes_sent<max_sent_bytes:
                round_no=summary.rounds_completed+1
                send_parallel_round(pool,clients,push_frame)
                summary.rounds_completed=round_no
                summary.messages_sent+=len(clients)
                summary.payload_bytes_sent+=len(clients)*args.payload_size
                summary.wire_bytes_sent+=len(clients)*len(push_frame)
                print(f"[+] Round {round_no}: {summary.payload_bytes_sent/(1024*1024):.1f} MiB sent")
                time.sleep(args.monitor_delay)
                status,detail=probe_connection(args)
                summary.probes.append(ProbeRecord(round_no,status,detail))
                print(f"[i] Probe round {round_no}: {status} ({detail})")
                if status=="rejected": consecutive+=1
                elif status=="accepted": consecutive=0
                if consecutive>=args.rejection_confirmations:
                    summary.rejection_detected=True
                    break
        if not summary.rejection_detected:
            print("[!] Safety stop reached before rejection",file=sys.stderr); return 2
        time.sleep(args.post_rejection_hold)
        summary.liveness_ok=verify_existing_session(clients[0])
        if not summary.liveness_ok:
            print("[!] Existing session not responsive",file=sys.stderr); return 3
        print("[+] Quota rejection confirmed and existing session is responsive")
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"[!] Load generator failed: {exc}",file=sys.stderr); return 1
    finally:
        summary.duration_seconds=round(time.monotonic()-started,3)
        for c in clients:
            try:c.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            c.close()
        write_summary(args.summary_json,summary)
        print(f"[i] Final: connected={summary.clients_connected}, rounds={summary.rounds_completed}, rejection={summary.rejection_detected}, liveness={summary.liveness_ok}")

if __name__=="__main__":
    sys.exit(main())
