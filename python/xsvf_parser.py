#!/usr/bin/env python3
import sys, os, math
import time, enum
from serial import Serial
from serial import SerialException

# xsvf_parser.py
ver = '1.0'
port = '/dev/ttyACM0'

class x_state(enum.Enum):
    XCOMPLETE    = 0  # 0
    XTDOMASK     = 1  # 1
    XSIR         = 2  # 2
    XSDR         = 3  # 3
    XRUNTEST     = 4  # 4
    XREPEAT      = 7  # 7
    XSDRSIZE     = 8  # 8
    XSDRTDO      = 9  # 9
    XSETSDRMASKS = 10 # A
    XSDRINC      = 11 # B
    XSDRB        = 12 # C
    XSDRC        = 13 # D
    XSDRE        = 14 # E
    XSDRTDOB     = 15 # F
    XSDRTDOC     = 16 # 10
    XSDRTDOE     = 17 # 11
    XSTATE       = 18 # 12
    XSIR2       = 254
    XIDLE       = 255

class bcolors:
    FAIL = '\033[91m'    #red
    OKGREEN = '\033[92m' #green
    WARNING = '\033[93m' #yellow
    OKBLUE = '\033[94m'  #dblue
    HEADER = '\033[95m'  #purple
    OKCYAN = '\033[96m'  #cyan
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
#print(f'{bcolors.FAIL}{bcolors.ENDC}')
#print(f'{bcolors.OKGREEN}{bcolors.ENDC}')
#print(f'{bcolors.WARNING}{bcolors.ENDC}')
#print(f'{bcolors.OKBLUE}{bcolors.ENDC}')
#print(f'{bcolors.HEADER}{bcolors.ENDC}')
#print(f'{bcolors.OKCYAN}{bcolors.ENDC}')

#----------------------------------
def progressbar(it, prefix="", size=60, out=sys.stdout): # Python3.6+
    count = len(it)
    start = time.time() # time estimate start
    def show(j):
        x = int(size*j/count)
        # time estimate calculation and string
        remaining = ((time.time() - start) / j) * (count - j)        
        mins, sec = divmod(remaining, 60) # limited to minutes
        time_str = f"{int(mins):02}:{sec:03.1f}"
        print(f"{prefix}[{u'█'*x}{('.'*(size-x))}] {j}/{count} Est wait {time_str}", end='\r', file=out, flush=True)
    show(0.1) # avoid div/0 
    for i, item in enumerate(it):
        yield item
        show(i+1)
    print("\n", flush=True, file=out)
#----------------------------------
def read_file(fn):
    try:
        with open(fn, "rb") as f:
            #print('File open')
            img = f.read()
            f.close()
            return img
    except Exception as e:
        print(f'File {fn} not found!')
        exit()

#----------------------------------
def dump_val(state, buf, pos, sdr=0):
    if sdr == None:
        sdr = 0
    print(f'State: {x_state(state).name}, Len: {len(buf)} Pos: 0x{pos:04X} SDR: 0x{sdr:04X} ', end = '')
    for idx,itm in enumerate(buf):
        print(f'0x{itm:02X} ', end = '')
    print('')

def xsvf_parser(ser, f):
    state  = x_state.XIDLE
    start = stop = 0
    length = 0
    sdr_bytes = 0
    for idx,itm in enumerate(f):
    #for idx in progressbar(range(len(f)), "Computing: ", 40):
        itm = f[idx]
        #print(f'Byte: 0x{itm:02X} @ pos: {idx}')
        if (state != x_state.XIDLE):
            if (idx == stop):
                if state == x_state.XSDRSIZE:
                    sdr_bytes = 0
                    sdr_bytes |= f[stop-1]<<8
                    #print(f'Val1: 0x{f[stop-1]}, sdr: 0x{sdr_bytes:04X}')
                    sdr_bytes |= f[stop]
                    #print(f'Val2: 0x{f[stop]}, sdr: 0x{sdr_bytes:04X}')
                    sdr_bytes = (sdr_bytes + 7)>>3
                    #print(f'sdr: 0x{idx:02X}, 0x{start:02X}, 0x{stop:02X}')
                    #print(f'sdr: 0x{sdr_bytes:04X}')

                if state == x_state.XSIR2:
                    start = idx + 1
                    stop = idx + (f[idx]>>3)
                    #print(f'sir2: 0x{idx:02X}, 0x{start:02X}, 0x{stop:02X}')
                    state = x_state.XSIR
                else:
                    #print(f'Dumping: {start} - {stop}, {f[start:stop+1]}')
                    dump_val(state, f[start:stop+1], start, sdr_bytes)
                    state = x_state.XIDLE
                    #print(f'State: {x_state(state).name}')
        else:
            match itm:
                case 0:
                    state = x_state.XCOMPLETE
                    length = 0
                case 1:
                    state = x_state.XTDOMASK
                    length = sdr_bytes
                case 2:
                    state = x_state.XSIR2
                    length = 1
                case 3:
                    state = x_state.XSDR
                    length = sdr_bytes
                case 4:
                    state = x_state.XRUNTEST
                    length = 4
                case 7:
                    state = x_state.XREPEAT
                    length = 1
                case 8:
                    state = x_state.XSDRSIZE
                    length = 4
                case 9:
                    state = x_state.XSDRTDO
                    length = sdr_bytes*2
                case 0x0A:
                    state = x_state.XSETSDRMASKS
                    length = sdr_bytes*2
                case 0x0C:
                    state = x_state.XSDRB
                    length = sdr_bytes
                case 0x0D:
                    state = x_state.XSDRC
                    length = sdr_bytes
                case 0x0E:
                    state = x_state.XSDRE
                    length = sdr_bytes
                case 0x0F:
                    state = x_state.XSDRTDOB
                    length = sdr_bytes*2
                case 0x10:
                    state = x_state.XSDRTDOC
                    length = sdr_bytes*2
                case 0x11:
                    state = x_state.XSDRTDOE
                    length = sdr_bytes*2
                case 0x12:
                    state = x_state.XSTATE
                    length = 1
                case _:
                    print(f'Unrecogized Command: 0x{itm:02X}')
                    pass

            start = idx + 1
            stop = idx + length

def main(ser, f):
    #print(f'In Main')
    xsvf_parser(ser, f)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'{bcolors.FAIL}Usage: ./xsvf_parser.py test.xsvf{bcolors.ENDC}')
        exit()
    os.system('clear')
    print(f'Scriptversion: {ver}')
    infile = sys.argv[1]

    if (os.path.isfile(infile) == True):
        print(f'{bcolors.OKCYAN}XSVF file found: {infile}{bcolors.ENDC}')
        f = read_file(infile)
    else:
        print(f'{bcolors.FAIL}XSVF file not found!{bcolors.ENDC}')
        exit()

    print(f'Size of Infile: {len(f)} byte.')
    try:
        ser = Serial(port, 921200, timeout = 1, writeTimeout = 1)
    except IOError:
        print('Port not found!')
        exit()

    ser.flush()
    main(ser, f)
    try:
        ser.close()
    except:
        pass    
    #write_file(df, outfile)
