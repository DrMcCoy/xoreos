/* eos - A reimplementation of BioWare's Aurora engine
 * Copyright (c) 2010 Sven Hesse (DrMcCoy), Matthew Hoops (clone2727)
 *
 * The Infinity, Aurora, Odyssey and Eclipse engines, Copyright (c) BioWare corp.
 * The Electron engine, Copyright (c) Obsidian Entertainment and BioWare corp.
 *
 * This file is part of eos and is distributed under the terms of
 * the GNU General Public Licence. See COPYING for more informations.
 */

/** @file sound/sound.cpp
 *  The global sound manager, handling all sound output.
 */

#include "sound/sound.h"
#include "sound/audiostream.h"
#include "sound/decoders/mp3.h"
#include "sound/decoders/vorbis.h"

#include "common/stream.h"
#include "common/util.h"
#include "common/error.h"

#include "graphics/graphics.h"

#include "events/events.h"

DECLARE_SINGLETON(Sound::SoundManager)

namespace Sound {

#define NUM_CHANNELS 2
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 4096

SoundManager::SoundManager() {
	_ready = false;
}

void SoundManager::init() {
	if (!GfxMan.ready())
		throw Common::Exception("The GraphicsManager needs to be initialized first");

	_dev = alcOpenDevice(NULL);
	if (!_dev)
		throw Common::Exception("Could not open OpenAL device");

	_ctx = alcCreateContext(_dev, NULL);
	alcMakeContextCurrent(_ctx);
	if (!_ctx)
		throw Common::Exception("Could not create OpenAL context");

	if (!createThread())
		throw Common::Exception("Failed to create sound thread: %s", SDL_GetError());

	_ready = true;
}

void SoundManager::deinit() {
	if (!_ready)
		return;

	if (!destroyThread())
		warning("SoundManager::deinit(): Sound thread had to be killed");

	for (uint32 i = 0; i < _channels.size(); i++)
		if (_channels[i])
			freeChannel(i);

	_channels.clear();

	alcMakeContextCurrent(NULL);
	alcDestroyContext(_ctx);
	alcCloseDevice(_dev);

	_ready = false;
}

bool SoundManager::ready() const {
	return _ready;
}

bool SoundManager::isPlaying(uint32 channel) const {
	if (channel >= _channels.size() || !_channels[channel])
		return false;

	ALint val;
	alGetSourcei(_channels[channel]->source, AL_SOURCE_STATE, &val);

	return val == AL_PLAYING;
}

AudioStream *SoundManager::makeAudioStream(Common::SeekableReadStream *stream) {
	bool isMP3 = false;
	uint32 tag = stream->readUint32BE();

	if (tag == 0xfff360c4) {
		// Modified WAVE file (used in streamsounds folder, at least in KotOR 1/2)
		stream = new Common::SeekableSubReadStream(stream, 0x1D6, stream->size(), DisposeAfterUse::YES);
	} else if (tag == MKID_BE('RIFF')) {
		stream->seek(12);
		tag = stream->readUint32BE();
		if (tag != MKID_BE('fmt ')) {
			warning("SoundManager::makeAudioStream(): Broken WAVE file");
			return 0;
		}

		// Skip fmt chunk
		stream->skip(stream->readUint32LE());
		tag = stream->readUint32BE();

		if (tag == MKID_BE('fact')) {
			// Skip useless chunk and dummied 'data' header
			stream->skip(stream->readUint32LE());
			tag = stream->readUint32BE();
		}

		if (tag != MKID_BE('data')) {
			warning("SoundManager::makeAudioStream(): Found invalid tag in WAVE file: %x", tag);
			return 0;
		}

		uint32 dataSize = stream->readUint32LE();
		if (dataSize == 0) {
			isMP3 = true;
			stream = new Common::SeekableSubReadStream(stream, stream->pos(), stream->size(), DisposeAfterUse::YES);
		} else // Just a regular WAVE
			stream->seek(0);
	} else if ((tag == MKID_BE('BMU ')) && (stream->readUint32BE() == MKID_BE('V1.0'))) {
		// BMU files: MP3 with extra header
		isMP3 = true;
		stream = new Common::SeekableSubReadStream(stream, stream->pos(), stream->size(), DisposeAfterUse::YES);
	} else if (tag == MKID_BE('OggS')) {
		stream->seek(0);
		return makeVorbisStream(stream, DisposeAfterUse::YES);
	} else {
		warning("Unknown sound format.");
		return 0;
	}

	if (isMP3)
		return makeMP3Stream(stream, DisposeAfterUse::YES);

	// TODO: WAVE/OGG
	warning("TODO: WAVE/Ogg");
	delete stream;

	return 0;
}

int SoundManager::playAudioStream(AudioStream *audStream) {
	if (!_ready)
		return -1;

	if (!audStream) {
		warning("SoundManager::playAudioStream(): No stream");
		return -1;
	}

	Common::StackLock lock(_mutex);

	Channel *channel = new Channel;
	channel->stream = audStream;

	if (!channel->stream) {
		warning("SoundManager::playAudioStream(): Could not detect stream type");
		return -1;
	}

	// Create the source and buffers and then begin playing the sound.
	alGenSources(1, &channel->source);

	channel->buffers = new ALuint[NUM_OPENAL_BUFFERS];
	alGenBuffers(NUM_OPENAL_BUFFERS, channel->buffers);

	// Fill the initial buffers with data.
	for (int i = 0; i < NUM_OPENAL_BUFFERS; i++)
		fillBuffer(channel->source, channel->buffers[i], channel->stream);

	alSourceQueueBuffers(channel->source, NUM_OPENAL_BUFFERS, channel->buffers);
	alSourcePlay(channel->source);

	if (alGetError() != AL_NO_ERROR)
		throw Common::Exception("OpenAL error while attempting to play");

	for (uint32 i = 0; i < _channels.size(); i++) {
		if (_channels[i] == 0) {
			_channels[i] = channel;
			return i;
		}
	}

	_channels.push_back(channel);
	return _channels.size() - 1;
}

int SoundManager::playSoundFile(Common::SeekableReadStream *wavStream) {
	if (!_ready)
		return -1;

	if (!wavStream) {
		warning("SoundManager::playSoundFile(): No stream");
		return -1;
	}

	return playAudioStream(makeAudioStream(wavStream));
}

void SoundManager::fillBuffer(ALuint source, ALuint alBuffer, AudioStream *stream) {
	if (!stream)
		throw Common::Exception("SoundManager::fillBuffer(): stream is 0");

	if (stream->endOfData())
		return;

	// Read in the required amount of samples
	uint32 numSamples = BUFFER_SIZE / 2;

	if (stream->isStereo())
		numSamples /= 2;

	byte *buffer = new byte[BUFFER_SIZE];
	memset(buffer, 0, BUFFER_SIZE);
	stream->readBuffer((int16 *)buffer, numSamples);

	alBufferData(alBuffer, stream->isStereo() ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, buffer, BUFFER_SIZE, stream->getRate());

	if (alGetError() != AL_NO_ERROR)
		throw Common::Exception("OpenAL error while filling buffer");
}

void SoundManager::bufferData(uint32 channel) {
	if (!_channels[channel])
		return;

	bufferData(_channels[channel]);
}

void SoundManager::bufferData(Channel *channel) {
	if (!channel || !channel->stream || channel->stream->endOfData())
		return;

	// Here we check how many buffers have been processed by OpenAL.
	// If we have any that haven't been processed, fill them with
	// more data from the AudioStream.

	ALint buffersProcessed = 0;
	alGetSourcei(channel->source, AL_BUFFERS_PROCESSED, &buffersProcessed);

	if (buffersProcessed <= 0)
		return;

	while (buffersProcessed--) {
		// Pull off the unused buffer from the queue, fill it, and throw it back on
		ALuint alBuffer;
		alSourceUnqueueBuffers(channel->source, 1, &alBuffer);
		fillBuffer(channel->source, alBuffer, channel->stream);
		alSourceQueueBuffers(channel->source, 1, &alBuffer);
	}
}

void SoundManager::update() {
	Common::StackLock lock(_mutex);

	for (uint i = 0; i < _channels.size(); i++) {
		if (!_channels[i] || !_channels[i]->stream)
			continue;

		// Free the channel if it is no longer playing
		if (!isPlaying(i)) {
			freeChannel(i);
			continue;
		}

		// Try to buffer some more data
		bufferData(i);
	}
}

void SoundManager::freeChannel(uint32 channel) {
	if (channel >= _channels.size() || !_channels[channel])
		return;

	Channel *c = _channels[channel];

	delete c->stream;
	alDeleteSources(1, &c->source);
	alDeleteBuffers(NUM_OPENAL_BUFFERS, c->buffers);
	delete[] c->buffers;

	delete c;
	_channels[channel] = 0;
}

void SoundManager::threadMethod() {
	while (!_killThread) {
		update();
		EventMan.delay(100);
	}
}

} // End of namespace Sound
