#pragma once
#include "stdafx.h"
#include "SnesMemoryType.h"

struct GbCpuState
{
	uint64_t CycleCount;
	uint16_t PC;
	uint16_t SP;

	uint8_t A;
	uint8_t Flags;

	uint8_t B;
	uint8_t C;
	uint8_t D;
	uint8_t E;
	
	uint8_t H;
	uint8_t L;

	bool EiPending;
	bool IME;
	bool Halted;
};

namespace GbCpuFlags
{
	enum GbCpuFlags
	{
		Zero = 0x80,
		AddSub = 0x40,
		HalfCarry = 0x20,
		Carry = 0x10
	};
}

namespace GbIrqSource
{
	enum GbIrqSource
	{
		VerticalBlank = 0x01,
		LcdStat = 0x02,
		Timer = 0x04,
		Serial = 0x08,
		Joypad = 0x10
	};
}

class Register16
{
	uint8_t* _low;
	uint8_t* _high;
	
public:
	Register16(uint8_t* high, uint8_t* low)
	{
		_high = high;
		_low = low;
	}

	uint16_t Read()
	{
		return (*_high << 8) | *_low;
	}

	void Write(uint16_t value)
	{
		*_high = (uint8_t)(value >> 8);
		*_low = (uint8_t)value;
	}

	void Inc()
	{
		Write(Read() + 1);
	}

	void Dec()
	{
		Write(Read() - 1);
	}

	operator uint16_t() { return Read(); }
};

enum class PpuMode
{
	HBlank,
	VBlank,
	OamEvaluation,
	Drawing
};

namespace GbPpuStatusFlags
{
	enum GbPpuStatusFlags
	{
		CoincidenceIrq = 0x40,
		OamIrq = 0x20,
		VBlankIrq = 0x10,
		HBlankIrq = 0x08
	};
}

struct GbPpuState
{
	uint8_t Scanline;
	uint16_t Cycle;
	PpuMode Mode;
	bool StatIrqFlag;

	uint8_t LyCompare;
	bool LyCoincidenceFlag;
	uint8_t BgPalette;
	uint8_t ObjPalette0;
	uint8_t ObjPalette1;
	uint8_t ScrollX;
	uint8_t ScrollY;
	uint8_t WindowX;
	uint8_t WindowY;

	uint8_t Control;
	bool LcdEnabled;
	bool WindowTilemapSelect;
	bool WindowEnabled;
	bool BgTileSelect;
	bool BgTilemapSelect;
	bool LargeSprites;
	bool SpritesEnabled;
	bool BgEnabled;

	uint8_t Status;
	uint32_t FrameCount;

	uint8_t CgbVramBank;
	
	uint8_t CgbBgPalPosition;
	bool CgbBgPalAutoInc;
	uint16_t CgbBgPalettes[4 * 8];
	
	uint8_t CgbObjPalPosition;
	bool CgbObjPalAutoInc;
	uint16_t CgbObjPalettes[4 * 8];
};

struct GbDmaControllerState
{
	uint8_t OamDmaDest;
	uint8_t DmaStartDelay;
	uint8_t InternalDest;
	uint8_t DmaCounter;
	uint8_t DmaReadBuffer;

	uint16_t CgbDmaSource;
	uint16_t CgbDmaDest;
	uint8_t CgbDmaLength;
	bool CgbHdmaMode;
};

struct GbSquareState
{
	uint16_t SweepPeriod;
	bool SweepNegate;
	uint8_t SweepShift;

	uint16_t SweepTimer;
	bool SweepEnabled;
	uint16_t SweepFreq;

	uint8_t Volume;
	uint8_t EnvVolume;
	bool EnvRaiseVolume;
	uint8_t EnvPeriod;
	uint8_t EnvTimer;

	uint8_t Duty;
	uint16_t Frequency;

	uint8_t Length;
	bool LengthEnabled;

	bool Enabled;
	uint16_t Timer;
	uint8_t DutyPos;
	uint8_t Output;
};

struct GbNoiseState
{
	uint8_t Volume;
	uint8_t EnvVolume;
	bool EnvRaiseVolume;
	uint8_t EnvPeriod;
	uint8_t EnvTimer;

	uint8_t Length;
	bool LengthEnabled;

	uint16_t ShiftRegister;

	uint8_t PeriodShift;
	uint8_t Divisor;
	bool ShortWidthMode;

	bool Enabled;
	uint32_t Timer;
	uint8_t Output;
};

struct GbWaveState
{
	bool DacEnabled;

	uint8_t SampleBuffer;
	uint8_t Ram[0x10];
	uint8_t Position;

	uint8_t Volume;
	uint16_t Frequency;

	uint16_t Length;
	bool LengthEnabled;

	bool Enabled;
	uint16_t Timer;
	uint8_t Output;
};

struct GbApuState
{
	bool ApuEnabled;

	uint8_t EnableLeftSq1;
	uint8_t EnableLeftSq2;
	uint8_t EnableLeftWave;
	uint8_t EnableLeftNoise;

	uint8_t EnableRightSq1;
	uint8_t EnableRightSq2;
	uint8_t EnableRightWave;
	uint8_t EnableRightNoise;

	uint8_t LeftVolume;
	uint8_t RightVolume;

	bool ExtAudioLeftEnabled;
	bool ExtAudioRightEnabled;

	uint8_t FrameSequenceStep;
};

struct GbApuDebugState
{
	GbApuState Common;
	GbSquareState Square1;
	GbSquareState Square2;
	GbWaveState Wave;
	GbNoiseState Noise;
};

enum class RegisterAccess
{
	None = 0,
	Read = 1,
	Write = 2,
	ReadWrite = 3
};

enum class GbMemoryType
{
	None = 0,
	PrgRom = (int)SnesMemoryType::GbPrgRom,
	WorkRam = (int)SnesMemoryType::GbWorkRam,
	CartRam = (int)SnesMemoryType::GbCartRam,
};

struct GbMemoryManagerState
{
	uint8_t CgbWorkRamBank;
	bool CgbSwitchSpeedRequest;
	bool CgbHighSpeed;
	uint64_t ApuCycleCount;
	bool DisableBootRom;
	uint8_t IrqRequests;
	uint8_t IrqEnabled;
	uint8_t InputSelect;

	bool IsReadRegister[0x100];
	bool IsWriteRegister[0x100];

	GbMemoryType MemoryType[0x100];
	uint32_t MemoryOffset[0x100];
	RegisterAccess MemoryAccessType[0x100];
};

enum class GbType
{
	Gb = 0,
	Cgb = 1,
};

struct GbState
{
	GbType Type;
	GbCpuState Cpu;
	GbPpuState Ppu;	
	GbApuDebugState Apu;
	GbMemoryManagerState MemoryManager;
	bool HasBattery;
};
