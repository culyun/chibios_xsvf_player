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

def write_file(data, fn):
    if fn == '':
        dump_data(data)
        exit()
    else:
        with open(fn, "wb") as f:
            #print(f'File open')
            f.write(bytes(data))
            f.close()

def dump_data(data, width):
    idx = len(data)
    for w in range(width):
        print (f' {w:02}  ', end = '')
    print(f'')
    for i in range(0, len(data), width):
        #print(f'idx: {idx}')
        if idx < width:
            stop = idx
        else:
            stop = width
        for j in range(0,stop):
            print(f'{bcolors.OKCYAN}0x{data[i+j]:02X} ', end = '')
        idx -= width
        print(f'{bcolors.ENDC} - {(i+width):>2} ({(i+width):02X})\r')
        print(f'{bcolors.ENDC}\r')
#----------------------------------
def dump_val(state, buf, pos, sdr=0):
    if sdr == None:
        sdr = 0
    print(f'State: {x_state(state).name}, Len: {len(buf)} Pos: 0x{pos:04X} SDR: 0x{sdr:04X} ', end = '')
    for idx,itm in enumerate(buf):
        print(f'0x{itm:02X} ', end = '')
    print('')

def make_checksum(data):
    return (sum(data) & 0xff).to_bytes(1,'big')

def build_message(state, buf):
    message = bytearray(b'X') # XSVF Write
    message += (len(buf)+4).to_bytes(1,'big') # Coplete Message Length is Length + Message + Length + Checksum
    message += (int(x_state(state).value)).to_bytes(1,'big') # next is the state
    message += len(buf).to_bytes(1,'big') # only XSVF Message length
    message += (buf) # XSVF Message
    message += make_checksum(message) # Checksum

    return message

def command_plus(f):
    state  = x_state.XIDLE
    start = stop = 0
    length = 0
    sdr_bytes = 0
    for idx,itm in enumerate(f): 
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
                    message = build_message(state, f[start:stop+1])
                    dump_data(message,len(message))
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
    return 1;

def main(ser, infile):
    #print(f'In Main')
    while(1):
        if (command_plus(infile)):
            break;
    return '0'

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
        ser = Serial(port, 115200, timeout = 1, writeTimeout = 1)
    except IOError:
        print('Port not found!')
        exit()

    ser.flush()
    df = main(ser, f)
    
    #write_file(df, outfile)
