#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
#include <SD.h>

#include "session.h"
#include "bits.h"

#include "constants.h"

void init_session_header(uint8_t* buf, uint64_t timestamp, uint16_t channels, uint16_t freq)
{
	buf[0] = 0xec; // Magic #1.
	buf[1] = 0x09; // Magic #2.
	buf[2] = 0x00; // Format epoch.
	buf[3] = 0x00; // Unused.
	uint16_t be_freq = swap_endian_16(freq);
	memcpy(buf+4, &be_freq, 2);
	uint16_t be_channels = swap_endian_16(channels);
	memcpy(buf+6, &be_channels, 2);
	// Last eight bytes are for the timestamp.
	// XXX: this assumes we're compiled little-endian. Better to make it
	//      endianness-agnostic.
	uint64_t be_ts = swap_endian_64(timestamp);
	memcpy(buf+8, &be_ts, 8);
}

// Write `len` bytes from `buf` to the session file.
int session_write(session *s, uint8_t *buf, size_t len)
{
	int written = s->fh.write(buf, len);
	(s->fh).flush();
	return written;
}

// Write a frame the session file. `data` should contain `CHANNELS`
// samples from the device.
// Values in data and delta_t should both be big-endian.
int session_write_frame(session *s, uint8_t *data, uint32_t delta_t)
{
	uint8_t frame[FRAME_SIZE];
	int written;

	frame[0] = s->counter; // Frame counter
	s->counter++;
	s->frame_time = delta_t;
	memcpy(frame+1, &delta_t, 4); // Time since session start
	memset(frame+5, 0, 3); // Unused bytes
	// Memory usage can be reduced here if needed.
	memcpy(frame+8, data, FRAME_SAMPLE_SIZE * CHANNELS); // ECG samples
	/*
	#ifdef DEBUG
	int i;
	for (i = 0; i < FRAME_SIZE; i++) {
		Serial.print(frame[i], HEX);
		Serial.print(" ");
	}
	Serial.println("");
	#endif
	*/
	written = session_write(s, frame, FRAME_SIZE);
	if (written != FRAME_SIZE) {
		#ifdef DEBUG
		Serial.print("Tried to write frame of ");
		Serial.print(FRAME_SIZE);
		Serial.print(" bytes, only wrote ");
		Serial.print(written);
		Serial.println(" bytes.");
		#endif
		return -1;
	}
	return 0;
}

// Given a *char of length at least 11, get_fname stores a string of the
// form "ecgses.XXX", where XXX is a hexadecimal integer between 0x0 and
// 0xfff. If no such unused filename exists -1 will be returned,
// otherwise the value of the integer used for the suffix plus one
// (assuming contiguity of session files, this is likely to be valid for
// the next one).
int get_fname(char *fname, int base_suffix)
{
	int suf = base_suffix;
	strncpy(fname, "ecgses.", 7);
	char valid = 0;
	while (!valid && suf < 0x1000) {
		snprintf(fname+7, 4, "%03x", suf);
		if (!SD.exists(fname)) {
			valid = 1;
		}
		suf++;
	}
	return (valid ? suf : -1);
}

session* session_init(uint64_t timestamp, uint16_t channels, uint16_t freq)
{
	char fname[11];
	int next_suffix;
	int written;
	uint8_t buf[SESSION_HEADER_SIZE];
	session *s;

	s = (session*) malloc(sizeof(session));
	if (s == NULL) {
		return NULL;
	}
	next_suffix = get_fname(fname, 0);
	if (next_suffix < 0) {
		#ifdef DEBUG
		Serial.println("Could not find an unused filename.");
		Serial.println("Is the SD card full?");
		#endif
		free(s);
		return NULL;
	}
	s->next_suffix = next_suffix;
	s->init_time = timestamp;
	s->fh = SD.open(fname, FILE_WRITE);
	if (!s->fh) {
		#ifdef DEBUG
		Serial.print("Could not open file ");
		Serial.print(fname);
		Serial.println(" for writing.");
		#endif
		free(s);
		return NULL;
	}
	init_session_header(buf, timestamp, channels, freq);
	#ifdef DEBUG
	Serial.print("Writing session header: ");
	for (int i = 0; i < 16; i++) {
		Serial.print(buf[i], HEX);
		Serial.print(" ");
	}
	Serial.println("");
	#endif
	written = session_write(s, buf, SESSION_HEADER_SIZE);
	if (written != SESSION_HEADER_SIZE) {
		#ifdef DEBUG
		Serial.println("Error writing session header.");
		#endif
		return NULL;
	}
	return s;
}

// Close session file, free heap memory.
void session_close(session *s)
{
	s->fh.close();
	free(s);
}
