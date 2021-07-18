#include <pspkernel.h>
#include <pspsdk.h>
#include <pspctrl.h>
#include <pspaudio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("sound_recorder", 0x1000, 1, 1);
PSP_MAIN_THREAD_ATTR(0);
PSP_HEAP_SIZE_KB(20);

#define J_OPCODE	0x08000000
#define NOP		0x00000000

#define psprintf pspDebugScreenPrintf /* Note:psprintf != printf :) */


#define USB_ENABLE	1
#define USBSOUND_SAVE_DIR	"usbhostfs0:/MUSIC"


int FileExists(char *path)
{
    SceIoStat stat;

    return (sceIoGetstat(path, &stat) >= 0);   
}


typedef struct
{
	int		inited;
	int		samplecount;
	int		format;

	int		recordstart;
	SceUID	fd;
	u32		size;
} CHANNELRECORD;

typedef struct
{
	u32		ChunkID;
	u32		ChunkSize;
	u32		Format;
	u32		SubChunk1ID;
	u32		SubChunk1Size;
	u16		AudioFormat;
	u16		NumChannels;
	u32		SampleRate;
	u32		ByteRate;
	u16		BlockAlign;
	u16		BitsPerSample;
	u32		SubChunk2ID;
	u32		SubChunk2Size;
} WAVHeader;

typedef int (*SCE_AUDIO_RESERVE)(int channel, int samplecount, int format);
typedef int (*SCE_AUDIO_RELEASE)(int channel);
typedef int (*SCE_AUDIO_OUTPUT)(int channel, int vol, void *buf);
typedef int (*SCE_AUDIO_OUTPUT_PANNED)(int channel, int leftvol, int rightvol, void *buf);
typedef int (*SCE_AUDIO_CHANGE_CHANNEL_CONFIG)(int channel, int format);
typedef int (*SCE_AUDIO_SET_CHANNEL_DATALEN)(int channel, int samplecount);

CHANNELRECORD channels[PSP_AUDIO_CHANNEL_MAX];
int recording;

SCE_AUDIO_RESERVE sceAudioChReserve_Real;
SCE_AUDIO_RELEASE sceAudioChRelease_Real;
SCE_AUDIO_OUTPUT sceAudioOutput_Real, sceAudioOutputBlocking_Real;
SCE_AUDIO_OUTPUT_PANNED sceAudioOutputPanned_Real, sceAudioOutputPannedBlocking_Real;
SCE_AUDIO_CHANGE_CHANNEL_CONFIG sceAudioChangeChannelConfig_Real;
SCE_AUDIO_SET_CHANNEL_DATALEN sceAudioSetChannelDataLen_Real;

void startRecordingChannel(int ch)
{
	int i;
	char file[256];
	WAVHeader header;

	if (!channels[ch].inited || channels[ch].recordstart)
		return;

	for (i = 0; i < 10000; i++)
	{
#if USB_ENABLE
		sceIoMkdir(USBSOUND_SAVE_DIR,0777);
		sprintf(file, "%s/RECORD_ch%d_%04d.wav", USBSOUND_SAVE_DIR, ch, i);
#else
		sprintf(file, "ms0:/PSP/MUSIC/RECORD_ch%d_%04d.wav", ch, i);
#endif
		if (!FileExists(file))
			break;
	}

	if (i == 10000)
	{
#if USB_ENABLE
		sceIoMkdir(USBSOUND_SAVE_DIR,0777);
		sprintf(file, "%s/RECORD_ch%d_0000.wav", USBSOUND_SAVE_DIR, ch);
#else
		sprintf(file, "ms0:/PSP/MUSIC/RECORD_ch%d_0000.wav", ch);
#endif
	}

	channels[ch].fd = sceIoOpen(file, PSP_O_WRONLY|PSP_O_CREAT, 0777);

	if (channels[ch].fd < 0)
	{
	//	printf("I/O Error opening file %s for writing.\n", file);
		return;
	}

	header.ChunkID = 0x46464952; // "RIFF"
	header.ChunkSize = 0; // We don't know it yet
	header.Format = 0x45564157; // "WAVE"
	header.SubChunk1ID = 0x20746d66; // "fmt "
	header.SubChunk1Size = 16;
	header.AudioFormat = 0x0001; // LPCM
	header.NumChannels = (channels[ch].format == PSP_AUDIO_FORMAT_STEREO) ? 2 : 1;
	header.SampleRate = 44100;
	header.ByteRate = header.SampleRate * header.NumChannels * 2;
	header.BlockAlign = header.NumChannels * 2;
	header.BitsPerSample = 16;
	header.SubChunk2ID = 0x61746164; // "data"
	header.SubChunk2Size = 0; // We don't know it yet

	sceIoWrite(channels[ch].fd, &header, sizeof(WAVHeader));

	channels[ch].size = 0;
	channels[ch].recordstart = 1;
}

void startRecordingAllChannels()
{
	int i;

	for (i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++)
		startRecordingChannel(i);
}

void stopRecordingChannel(int ch)
{
	u32 n;

	if (!channels[ch].inited || !channels[ch].recordstart)
		return;	

	channels[ch].recordstart = 0;		

	sceIoLseek(channels[ch].fd, 4, PSP_SEEK_SET);
	n = channels[ch].size + sizeof(WAVHeader) - 8;
	sceIoWrite(channels[ch].fd, &n, 4);

	sceIoLseek(channels[ch].fd, 40, PSP_SEEK_SET);	
	sceIoWrite(channels[ch].fd, &channels[ch].size, 4);

	sceIoClose(channels[ch].fd);
	
	channels[ch].fd = -1;
	channels[ch].size = 0;
}

void stopRecordingAllChannels()
{
	int i;

	for (i = 0; i < PSP_AUDIO_CHANNEL_MAX; i++)
		stopRecordingChannel(i);
}

void outputSamples(int ch, u16 *samples)
{
	if (!channels[ch].inited || !channels[ch].recordstart)
		return;

	if (channels[ch].fd >= 0)
	{
		int size;

		size = channels[ch].samplecount << 1;

		if (channels[ch].format == PSP_AUDIO_FORMAT_STEREO)
			size <<= 1;
		
		sceIoWrite(channels[ch].fd, samples, size);

		channels[ch].size += size;
	}
}

int hooked_sceAudioChReserve(int channel, int samplecount, int format)
{
	int res=0;

	//printf("sceAudioChReserve called: channel %d, samplecount %d, format %d\n", channel, samplecount, format);

	// Restore original instructions
	/*_sw(0x27BDFFE0, 0x880C1F48);
	_sw(0xAFB3000C, 0x880C1F4C);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	res = sceAudioChReserve_Real(channel, samplecount, format);

	//printf("sceAudioChReserve result: %d\n", res);

	if (res >= 0 && res < PSP_AUDIO_CHANNEL_MAX)
	{
		channels[res].inited = 1;
		channels[res].recordstart = 0;
		channels[res].samplecount = samplecount;
		channels[res].format = format;
		channels[res].fd = -1;

		if (recording)
			startRecordingChannel(res);
	}

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioChReserve) & 0x3FFFFFFF) >> 2), 0x880C1F48);
	_sw(NOP, 0x880C1F4C);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}

int hooked_sceAudioChRelease(int channel)
{
	int res = 0;

	//printf("sceAudioChRelease called: channel %d\n", channel);

	// Restore original instructions
	/*_sw(0x27BDFFF0, 0x880C22D0);
	_sw(0xAFB00000, 0x880C22D4);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	res = sceAudioChRelease_Real(channel); 

	//printf("sceAudioChRelease result: %d\n", res);
	
	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX) 
	{
		if (recording)
			stopRecordingChannel(channel);

		channels[channel].inited = 0;		
	}
	

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioChRelease) & 0x3FFFFFFF) >> 2), 0x880C22D0);
	_sw(NOP, 0x880C22D4);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}

int hooked_sceAudioOutput(int channel, int vol, void *buf)
{
	int res;

	// Restore original instructions
	/*_sw(0x27BDFFE0, 0x880C1A2C);
	_sw(0x3403FFFF, 0x880C1A30);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	res = sceAudioOutput_Real(channel, vol, buf);
	
	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (recording)
			outputSamples(channel, buf);		
	}	

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioOutput) & 0x3FFFFFFF) >> 2), 0x880C1A2C);
	_sw(NOP, 0x880C1A30);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}

int hooked_sceAudioOutputBlocking(int channel, int vol, void *buf)
{
	int res;

	// Restore original instructions
	/*_sw(0x27BDFFD0, 0x880C1B10);
	_sw(0x3403FFFF, 0x880C1B14);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll(); */

	res = sceAudioOutputBlocking_Real(channel, vol, buf);

	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (recording)
			outputSamples(channel, buf);	
	}

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioOutputBlocking) & 0x3FFFFFFF) >> 2), 0x880C1B10);
	_sw(NOP, 0x880C1B14);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}


int hooked_sceAudioOutputPanned(int channel, int leftvol, int rightvol, void *buf)
{
	int res;

	// Restore original instructions
	/*_sw(0x3403FFFF, 0x880C1CAC);
	_sw(0x27BDFFE0, 0x880C1CB0);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	res = sceAudioOutputPanned_Real(channel, leftvol, rightvol, buf);

	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (recording)
			outputSamples(channel, buf);		
	}	

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioOutputPanned) & 0x3FFFFFFF) >> 2), 0x880C1CAC);
	_sw(NOP, 0x880C1CB0);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}

int hooked_sceAudioOutputPannedBlocking(int channel, int leftvol, int rightvol, void *buf)
{
	int res;

	// Restore original instructions
	/*_sw(0x27BDFFD0, 0x880C1DA4);
	_sw(0x00A61025, 0x880C1DA8);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	res = sceAudioOutputPannedBlocking_Real(channel, leftvol, rightvol, buf);
	
	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (recording)
			outputSamples(channel, buf);		
	}	

	// Restore hack
	/*_sw(J_OPCODE | (((u32)(hooked_sceAudioOutputPannedBlocking) & 0x3FFFFFFF) >> 2), 0x880C1DA4);
	_sw(NOP, 0x880C1DA8);
	sceKernelDcacheWritebackAll();
	sceKernelIcacheClearAll();*/

	return res;
}

int hooked_sceAudioChangeChannelConfig(int channel, int format)
{
	int res;

	//printf("sceAudioChangeChannelConfig called.\n");
	//printf("channel = %d, format = %d\n", channel, format);

	res = sceAudioChangeChannelConfig_Real(channel, format);

	//printf("result = %d\n", res);

	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (channels[channel].inited)
			// Uurghhh, if record started, this won't be beaitiful :)
			channels[channel].format  = format; 
	}

	return res;
}

int hooked_sceAudioSetChannelDataLen(int channel, int samplecount)
{
	int res;

	//printf("sceAudioSetChannelDataLen called.\n");
	//printf("channel = %d, samplecount = %d\n", channel, samplecount);

	res = sceAudioSetChannelDataLen_Real(channel, samplecount);

	//printf("result = %d\n", res);

	if (channel >= 0 && channel < PSP_AUDIO_CHANNEL_MAX)
	{
		if (channels[channel].inited)
			channels[channel].samplecount  = samplecount;
	}

	return res;
}


u32 findSyscall(u32 address)
{
	return (*(u32*)(address + 4) >> 6);
}

//#define VECTORS_OFFSET 0x8801e830	//for 1.50	Working
//#define VECTORS_OFFSET 0x8801F430	//for 2.00	Working
//#define VECTORS_OFFSET 0x880208a8	//for 2.50	Not Working
//#define VECTORS_OFFSET 0x88023380	//for 2.60	Not Working
//#define VECTORS_OFFSET 0x880219d8	//for 2.71	Not Working

int init()
{
	memset(channels, 0, sizeof(channels));
	recording = 0;
	u32 offset = 0;
	u32 VECTORS_OFFSET = 0;
	
	switch(sceKernelDevkitVersion())
	{
	case 0x01050001://1.50 Working
		VECTORS_OFFSET = 0x8801e830;
		break;

	case 0x02000010://2.00 Working
		VECTORS_OFFSET = 0x8801F430;
		break;

	case 0x02050010://2.50 Not Working
		VECTORS_OFFSET = 0x880208a8;
		break;

	case 0x02060010://2.60 Not Working
		VECTORS_OFFSET = 0x88023380;
		break;

	case 0x02070110://2.71 Not Working
		VECTORS_OFFSET = 0x880219d8;
		break;

	case 0x01000300://1.00
	case 0x01050100://1.51
	case 0x01050200://1.52
	case 0x02070010://2.70
	default:
		return 1;
	}

	offset = (findSyscall((u32)sceAudioChReserve) << 2) + VECTORS_OFFSET;
	sceAudioChReserve_Real = (SCE_AUDIO_RESERVE)_lw(offset);
	_sw((u32)hooked_sceAudioChReserve, offset);

	offset = (findSyscall((u32)sceAudioChRelease) << 2) + VECTORS_OFFSET;
	sceAudioChRelease_Real = (SCE_AUDIO_RELEASE)_lw(offset);
	_sw((u32)hooked_sceAudioChRelease, offset);

	offset = (findSyscall((u32)sceAudioOutput) << 2) + VECTORS_OFFSET;
	sceAudioOutput_Real = (SCE_AUDIO_OUTPUT)_lw(offset);
	_sw((u32)hooked_sceAudioOutput, offset);

	offset = (findSyscall((u32)sceAudioOutputBlocking) << 2) + VECTORS_OFFSET;
	sceAudioOutputBlocking_Real = (SCE_AUDIO_OUTPUT)_lw(offset);
	_sw((u32)hooked_sceAudioOutputBlocking, offset);

	offset = (findSyscall((u32)sceAudioOutputPanned) << 2) + VECTORS_OFFSET;
	sceAudioOutputPanned_Real = (SCE_AUDIO_OUTPUT_PANNED)_lw(offset);
	_sw((u32)hooked_sceAudioOutputPanned, offset);

	offset = (findSyscall((u32)sceAudioOutputPannedBlocking) << 2) + VECTORS_OFFSET;
	sceAudioOutputPannedBlocking_Real = (SCE_AUDIO_OUTPUT_PANNED)_lw(offset);
	_sw((u32)hooked_sceAudioOutputPannedBlocking, offset);

	offset = (findSyscall((u32)sceAudioChangeChannelConfig) << 2) + VECTORS_OFFSET;
	sceAudioChangeChannelConfig_Real = (SCE_AUDIO_CHANGE_CHANNEL_CONFIG)_lw(offset);
	_sw((u32)hooked_sceAudioChangeChannelConfig, offset);

	offset = (findSyscall((u32)sceAudioSetChannelDataLen) << 2) + VECTORS_OFFSET;
	sceAudioSetChannelDataLen_Real = (SCE_AUDIO_SET_CHANNEL_DATALEN)_lw(offset);
	_sw((u32)hooked_sceAudioSetChannelDataLen, offset);

	sceKernelDcacheWritebackAll();
	//sceKernelIcacheClearAll();
	
	return 0;
}

/***************************************

メインスレッド

****************************************/
int thread_start(SceSize args, void *argp)
{

	SceCtrlData pad;

//	sceCtrlSetSamplingCycle(0);
//	sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

	//初期化
	if(init() != 0) return 0;

	while (1)
	{
		int keyprocessed;

		keyprocessed = 0;
		sceCtrlPeekBufferPositive(&pad, 1);

		if ((pad.Buttons & PSP_CTRL_LTRIGGER) && 
			(pad.Buttons & PSP_CTRL_RTRIGGER) &&
			(pad.Buttons & PSP_CTRL_CIRCLE))
		{
		//	printf("Audio record begin.\n");

			startRecordingAllChannels();

			recording = 1;
			keyprocessed = 1;
		}

		else 
		if ((pad.Buttons & PSP_CTRL_LTRIGGER) && 
			(pad.Buttons & PSP_CTRL_RTRIGGER) &&
			(pad.Buttons & PSP_CTRL_SQUARE))
		{
		//	printf("Audio record end.\n");

			stopRecordingAllChannels();
			
			recording = 0;
			keyprocessed = 1;
		}

		sceKernelDelayThread((keyprocessed) ? 1000000 : 50000);
	}

	return 0;
}

/***************************************

モジュールスタート

****************************************/
int module_start(SceSize args, void *argp)
{
	SceUID thid = sceKernelCreateThread("soundrecorder", thread_start, 0x16,
		0x00010000, 0, NULL);

	if (thid < 0)
	{		
		return -1;
	}

	sceKernelStartThread(thid, args, argp);
	return 0;
}
