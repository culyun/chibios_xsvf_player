#!/usr/bin/env python3
import sys, os, math
import time, enum
from serial import Serial
from serial import SerialException

# xsvf_parser.py
ver = '1.0'
port = '/dev/ttyACM0'

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

def make_checksum(data):
    return sum(data) & 0xff

#----------------------------------
def write(channel, data):
    if isinstance(data, int):
        data = bytes((data,))

    try:
        result = channel.write(data)
        #print(f'Write Result: {result}')
        if result != len(data):
            raise Exception('write timeout')
        channel.flush()
    except:
        print("Write error!")
        #print(f'Write: {data}')
        #[print(f'Write: {e:02X}, {chr(e)}') for e in data]
        pass

def read(channel, size = 1):
    # Read a sequence of bytes
    if size == 0:
        return

    try:
        result = channel.read(size)
        #print(f'Read Result length: {len(result)}')
        if len(result) != size:
            print(f'Read error, Size: 0x{result:02X}')
            raise Exception('Read error')
    except:
        print('Read Error!')
        #result = (read_file('rom.bin'))[:size] #only for debug on a pc
    return result

def write_with_checksum(ser, data):
    cs_file = make_checksum(data)
    data += bytes([cs_file])
    write(ser, data)

def expect_ok(ser):
    if ser == '':
        response = b'O'
    else:
        response = read(ser)
    if response == b'X':
        print(f'Checksum or Programming Error')
    elif response == b'O':
        print(f'Response OK.')
    elif response == b'Y':
        print(f'Uplaod done.')
        while 1: # Wait for programming
            response = read(ser)
            if response == b'F': # Done Programming
                break
            else:
                #time.sleep(0.5)
                val = int.from_bytes(response, byteorder='big')*10
                print(f'Progress: {val:02d}%', end = '\r', file=sys.stdout, flush=True)

    else:
        raise Exception('Response error')

def split_file(data, size=16384):
    #file = []
    file = [data[i:i + size] for i in range(0, len(data), size)]  
    print(f'Length total: {len(data)}, chunksize in: {size} Chunks: {len(file)}')
    return file

def write_xsvf(ser, data):
    #print('------------------------------  XSVF  ------------------------------')
    length = len(data)
    #print (f'Length of data: {length} or 0x{length:04x}')

    message = bytearray(b'X') # XSVF Write
    #print(f'Header: X, ', end = '')
    len_hi, len_low = length.to_bytes(2, byteorder='big')
    print(f'Length High: {len_hi:02X} Low: {len_low:02X} Total: {len_hi*256+len_low}')
    message += bytes((len_hi,))
    message += bytes((len_low,))
    message += data
    #dump_data(message, 16)
    #cs_file = make_checksum(data)
    #print(f'Checksum: 0x{cs_file:02x}')
    write_with_checksum(ser, message)
    time.sleep(0.1)
    expect_ok(ser)
    print(f'{bcolors.OKCYAN}Done!{bcolors.ENDC}')

def main(ser, f):
    if len(f) > 32768:
        print(f'Split file in 32k Chunks')
        for idx,itm in enumerate(split_file(f)):
            print(f'Writing: Index: {idx} Length: {len(itm)}')
            write_xsvf(ser, itm)
    else:
        write_xsvf(ser, f)
        print(f'Writing: File')

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
    size = len(f)
#    if (size > 32768):
#        print(f'Size of Infile too big with {len(f)} byte. (32k MAX!)')
#        exit()
    try:
        ser = Serial(port, 921200, timeout = 10, writeTimeout = 1)
    except IOError:
        print('Port not found!')
        exit()

    ser.flush()
    main(ser, f)
    try:
        ser.close()
    except:
        pass    
