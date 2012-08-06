# serial is the pySerial module
import serial, sys, os
from time import sleep

# Transfers data over a serial ("RS-232", though not really) link to an Arduino.
# Thomas Backman, August 5 2012

ERR = 0xfc
RDY = 0xfd
ACK = 0xfe
END = 0x0

# Protocol documentation (quick and dirty; I wrote the protocol as I wrote this program!)
# S = sender, R = receiver. Data transmission is always uni-directional, though the receiver
# sends back ACK or ERR codes.

# 1) Sender sends a byte with the transfer size (1 - 255 bytes)
# 2) Receiver sends an ACK byte
# 3) Sender sends all the data in one go
# 4) Receiver sends an ACK byte
# 5) The receiver processes the received data.
# 6) Receiver sends a RDY byte when it is ready to receive further data.
# 7) The sender goes back to step 1, if there is more data to send.
#
# The loop is broken when the entire file is sent, after which
# 8) The sender sends an END byte (0x0) signalling the end of the transmission.

# On the receiver side, the above applies, with the addition of sending the ERR byte (after discarding
# all incoming bytes until "they stop coming") if there is a transmission error.

if len(sys.argv) != 2:
	print >> sys.stderr, 'Usage: {0} <filename to transfer> (1 - 131072 bytes)'.format(sys.argv[0])
	sys.exit(1)

filename = sys.argv[1]
print 'File to transfer:', filename

if not os.path.exists(filename):
	print >> sys.stderr, "File {0} doesn't exist! Exiting.".format(filename)
	sys.exit(2)

try:
	# The file is supposed be 128 kiB or less, so...
	data = open(filename).read()
except:
	print >> sys.stderr,  'Failed to read file data! Exiting.'
	sys.exit(4)

bytesToSend = len(data)

if bytesToSend > 131072:
	print >> sys.stderr, 'File is too big for the EEPROM! Exiting.'
	sys.exit(4)

print bytesToSend, 'bytes to transfer'

# OK, we have what we need! Let's see...

try:
	s = serial.Serial('/dev/tty.usbmodemfd121', 115200, timeout=6)
except:
	print >> sys.stderr, 'Error setting up the serial link. Exiting.'
	sys.exit(8)

bytesSent = 0

# The Arduino restarts above... Wait a while.
print 'Waiting 3 seconds for Arduino to finish reset... ',
sys.stdout.flush()
sleep(3)
print 'done!'

# Used for the progress display
last_len = 0
print 'Transfer progress:',

while bytesToSend > bytesSent:
	# Calculate and send the length of this chunk (1-128 bytes)
	chunksize = min(128, bytesToSend - bytesSent)
	s.write(chr(chunksize))
#print 'In loop. {0} bytes sent, {1} bytes to go. {2} bytes in this chunk'.format(bytesSent, bytesToSend - bytesSent, chunksize)

	# Did we get an ACK?
	response = s.read()
	if not (len(response) == 1 and ord(response) == ACK):
		print >> sys.stderr, "Remote end didn't acknowledge. Response: {0}".format(hex(ord(response)))
		print >> sys.stderr, "Exiting."
		sys.exit(16)
	
#print 'ACK received, sending data'
	# Send the data
	s.write(data[bytesSent : bytesSent + 128])

	# Listen for ACK
	response = s.read()
	if not (len(response) == 1 and ord(response) == ACK):
		print >> sys.stderr, "Remote end didn't acknowledge. Response: {0}".format(hex(ord(response)))
		print >> sys.stderr, "Exiting."
		sys.exit(16)
	
#print 'ACK received; write cycle underway. Waiting for RDY...'
	# We got an ACK; device should now be writing. Wait for the ready byte. If received, loop again.
	response = s.read()
	if not (len(response) == 1 and ord(response) == RDY):
		print >> sys.stderr, "Remote end didn't send ready byte in time. Response: {0}".format(hex(ord(response)))
		print >> sys.stderr, "Exiting."
		sys.exit(16)
	
#print 'RDY received. Looping...'

	bytesSent += chunksize

# Print progress
	for i in range (0, last_len): sys.stdout.write('\b')
	out = str(round(float(bytesSent)/bytesToSend * 100, 1)) + ' %'
	print out,
	last_len = len(out)
	sys.stdout.flush()

print "\nLoop finished."
if bytesToSend == bytesSent:
	s.write(chr(END))
	print 'Successfully transferred {0} bytes!'.format(bytesSent)
else:
	print 'Something bad happened! bytesToSend={0} bytesSent{1}'.format(bytesToSend, bytesSent)
	sys.exit(32)
