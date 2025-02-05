#include "stdafx.h"
#include "Console.h"
#include "Cpu.h"
#include "Ppu.h"
#include "Spc.h"
#include "NecDsp.h"
#include "InternalRegisters.h"
#include "ControlManager.h"
#include "MemoryManager.h"
#include "DmaController.h"
#include "BaseCartridge.h"
#include "RamHandler.h"
#include "Gameboy.h"
#include "GbPpu.h"
#include "Debugger.h"
#include "DebugTypes.h"
#include "NotificationManager.h"
#include "SoundMixer.h"
#include "VideoDecoder.h"
#include "VideoRenderer.h"
#include "DebugHud.h"
#include "MessageManager.h"
#include "KeyManager.h"
#include "EventType.h"
#include "EmuSettings.h"
#include "SaveStateManager.h"
#include "CartTypes.h"
#include "RewindManager.h"
#include "ConsoleLock.h"
#include "MovieManager.h"
#include "BatteryManager.h"
#include "CheatManager.h"
#include "MovieManager.h"
#include "SystemActionManager.h"
#include "Msu1.h"
#include "../Utilities/Serializer.h"
#include "../Utilities/Timer.h"
#include "../Utilities/VirtualFile.h"
#include "../Utilities/PlatformUtilities.h"
#include "../Utilities/FolderUtilities.h"

Console::Console()
{
	_settings.reset(new EmuSettings(this));

	_paused = false;
	_pauseOnNextFrame = false;
	_stopFlag = false;
	_lockCounter = 0;
	_threadPaused = false;
}

Console::~Console()
{
}

void Console::Initialize()
{
	_lockCounter = 0;

	_notificationManager.reset(new NotificationManager());
	_batteryManager.reset(new BatteryManager());
	_videoDecoder.reset(new VideoDecoder(shared_from_this()));
	_videoRenderer.reset(new VideoRenderer(shared_from_this()));
	_saveStateManager.reset(new SaveStateManager(shared_from_this()));
	_soundMixer.reset(new SoundMixer(this));
	_debugHud.reset(new DebugHud());
	_cheatManager.reset(new CheatManager(this));
	_movieManager.reset(new MovieManager(shared_from_this()));

	_videoDecoder->StartThread();
	_videoRenderer->StartThread();
}

void Console::Release()
{
	Stop(true);

	_videoDecoder->StopThread();
	_videoRenderer->StopThread();
	
	_videoDecoder.reset();
	_videoRenderer.reset();
	_debugHud.reset();
	_notificationManager.reset();
	_saveStateManager.reset();
	_soundMixer.reset();
	_settings.reset();
	_cheatManager.reset();
	_movieManager.reset();
}

void Console::RunFrame()
{
	_frameRunning = true;
	if(_settings->CheckFlag(EmulationFlags::GameboyMode)) {
		Gameboy* gameboy = _cart->GetGameboy();
		while(_frameRunning) {
			gameboy->Exec();
		}
	} else {
		while(_frameRunning) {
			_cpu->Exec();
		}
	}
}

void Console::ProcessEndOfFrame()
{
	_frameRunning = false;
}

void Console::RunSingleFrame()
{
	_controlManager->UpdateInputState();
	_internalRegisters->ProcessAutoJoypadRead();

	RunFrame();

	_cart->RunCoprocessors();
	if(_cart->GetCoprocessor()) {
		_cart->GetCoprocessor()->ProcessEndOfFrame();
	}

	_controlManager->UpdateControlDevices();
}

void Console::Stop(bool sendNotification)
{
	_stopFlag = true;

	_notificationManager->SendNotification(ConsoleNotificationType::BeforeGameUnload);

	_emulationLock.WaitForRelease();

	if(sendNotification) {
		_notificationManager->SendNotification(ConsoleNotificationType::BeforeEmulationStop);
	}

	_consoleType = ConsoleType::Snes;
	_settings->ClearFlag(EmulationFlags::GameboyMode);

	_videoDecoder->StopThread();
	_rewindManager.reset();

	_cpu.reset();
	_ppu.reset();
	_spc.reset();
	_cart.reset();
	_internalRegisters.reset();
	_controlManager.reset();
	_memoryManager.reset();
	_dmaController.reset();
	_msu1.reset();

	_soundMixer->StopAudio(true);

	if(sendNotification) {
		_notificationManager->SendNotification(ConsoleNotificationType::EmulationStopped);
	}
}

void Console::Reset()
{
	_lockCounter++;
	_runLock.Acquire();

	_dmaController->Reset();
	_internalRegisters->Reset();
	_memoryManager->Reset();
	_spc->Reset();
	_ppu->Reset();
	_cart->Reset();
	//_controlManager->Reset();

	//Reset cart before CPU to ensure correct memory mappings when fetching reset vector
	_cpu->Reset();

	_notificationManager->SendNotification(ConsoleNotificationType::GameReset);
	ProcessEvent(EventType::Reset);

	_runLock.Release(); 
	_lockCounter--;
}

void Console::ReloadRom(bool forPowerCycle)
{
	shared_ptr<BaseCartridge> cart = _cart;
	if(cart) {
		RomInfo info = cart->GetRomInfo();
		Lock();
		LoadRom(info.RomFile, info.PatchFile, false, forPowerCycle);
		Unlock();
	}
}

void Console::PowerCycle()
{
	ReloadRom(true);
}

bool Console::LoadRom(VirtualFile romFile, VirtualFile patchFile, bool stopRom, bool forPowerCycle)
{
	if(_cart) {
		//Make sure the battery is saved to disk before we load another game (or reload the same game)
		_cart->SaveBattery();
	}

	bool result = false;
	EmulationConfig orgConfig = _settings->GetEmulationConfig(); //backup emulation config (can be temporarily overriden to control the power on RAM state)
	shared_ptr<BaseCartridge> cart = forPowerCycle ? _cart : BaseCartridge::CreateCartridge(this, romFile, patchFile);
	if(cart) {
		if(stopRom) {
			KeyManager::UpdateDevices();
			Stop(false);
		}

		_cheatManager->ClearCheats(false);
		
		_cart = cart;
		
		_batteryManager->Initialize(FolderUtilities::GetFilename(romFile.GetFileName(), false));

		UpdateRegion();

		_internalRegisters.reset(new InternalRegisters());
		_memoryManager.reset(new MemoryManager());
		_ppu.reset(new Ppu(this));
		_controlManager.reset(new ControlManager(this));
		_dmaController.reset(new DmaController(_memoryManager.get()));
		_spc.reset(new Spc(this));

		_msu1.reset(Msu1::Init(romFile, _spc.get()));

		_cpu.reset(new Cpu(this));
		_memoryManager->Initialize(this);
		_internalRegisters->Initialize(this);

		if(_cart->GetCoprocessor() == nullptr && _cart->GetGameboy()) {
			_cart->GetGameboy()->PowerOn();
			_consoleType = _cart->GetGameboy()->IsCgb() ? ConsoleType::GameboyColor : ConsoleType::Gameboy;
			_settings->SetFlag(EmulationFlags::GameboyMode);
		} else {
			_consoleType = ConsoleType::Snes;
			_settings->ClearFlag(EmulationFlags::GameboyMode);
		}

		_ppu->PowerOn();
		_cpu->PowerOn();

		_rewindManager.reset(new RewindManager(shared_from_this()));
		_notificationManager->RegisterNotificationListener(_rewindManager);

		_controlManager->UpdateControlDevices();
				
		UpdateRegion();

		_notificationManager->SendNotification(ConsoleNotificationType::GameLoaded, (void*)forPowerCycle);

		_paused = false;

		if(!forPowerCycle) {
			string modelName = _region == ConsoleRegion::Pal ? "PAL" : "NTSC";
			string messageTitle = MessageManager::Localize("GameLoaded") + " (" + modelName + ")";
			MessageManager::DisplayMessage(messageTitle, FolderUtilities::GetFilename(GetRomInfo().RomFile.GetFileName(), false));
		}
		result = true;
	} else {
		MessageManager::DisplayMessage("Error", "CouldNotLoadFile", romFile.GetFileName());
	}

	_settings->SetEmulationConfig(orgConfig);
	return result;
}

RomInfo Console::GetRomInfo()
{
	shared_ptr<BaseCartridge> cart = _cart;
	if(cart) {
		return cart->GetRomInfo();
	} else {
		return {};
	}
}

uint64_t Console::GetMasterClock()
{
	if(_settings->CheckFlag(EmulationFlags::GameboyMode) && _cart->GetGameboy()) {
		return _cart->GetGameboy()->GetCycleCount();
	} else {
		return _memoryManager->GetMasterClock();
	}
}

uint32_t Console::GetMasterClockRate()
{
	return _masterClockRate;
}

ConsoleRegion Console::GetRegion()
{
	return _region;
}

ConsoleType Console::GetConsoleType()
{
	return _consoleType;
}

void Console::UpdateRegion()
{
	switch(_settings->GetEmulationConfig().Region) {
		case ConsoleRegion::Auto: _region = _cart->GetRegion(); break;

		default:
		case ConsoleRegion::Ntsc: _region = ConsoleRegion::Ntsc; break;
		case ConsoleRegion::Pal: _region = ConsoleRegion::Pal; break;
	}

	_masterClockRate = _region == ConsoleRegion::Pal ? 21281370 : 21477270;
}

double Console::GetFps()
{
	if(_settings->CheckFlag(EmulationFlags::GameboyMode)) {
		return 59.72750056960583;
	} else {
		if(_region == ConsoleRegion::Ntsc) {
			return _settings->GetVideoConfig().IntegerFpsMode ? 60.0 : 60.0988118623484;
		} else {
			return _settings->GetVideoConfig().IntegerFpsMode ? 50.0 : 50.00697796826829;
		}
	}
}

void Console::Pause()
{
	_paused = true;
}

void Console::Resume()
{
	_paused = false;
}

bool Console::IsPaused()
{
	return _paused;
}

ConsoleLock Console::AcquireLock()
{
	return ConsoleLock(this);
}

void Console::Lock()
{
	_lockCounter++;
	_runLock.Acquire();
}

void Console::Unlock()
{
	_runLock.Release();
	_lockCounter--;
}

void Console::Serialize(ostream &out, int compressionLevel)
{
	Serializer serializer(SaveStateManager::FileFormatVersion);
	bool isGameboyMode = _settings->CheckFlag(EmulationFlags::GameboyMode);

	if(!isGameboyMode) {
		serializer.Stream(_cpu.get());
		serializer.Stream(_memoryManager.get());
		serializer.Stream(_ppu.get());
		serializer.Stream(_dmaController.get());
		serializer.Stream(_internalRegisters.get());
		serializer.Stream(_cart.get());
		serializer.Stream(_controlManager.get());
		serializer.Stream(_spc.get());
		if(_msu1) {
			serializer.Stream(_msu1.get());
		}
	} else {
		serializer.Stream(_cart.get());
		serializer.Stream(_controlManager.get());
	}
	serializer.Save(out, compressionLevel);
}

void Console::Deserialize(istream &in, uint32_t fileFormatVersion, bool compressed)
{
	Serializer serializer(in, fileFormatVersion, compressed);
	bool isGameboyMode = _settings->CheckFlag(EmulationFlags::GameboyMode);

	if(!isGameboyMode) {
		serializer.Stream(_cpu.get());
		serializer.Stream(_memoryManager.get());
		serializer.Stream(_ppu.get());
		serializer.Stream(_dmaController.get());
		serializer.Stream(_internalRegisters.get());
		serializer.Stream(_cart.get());
		serializer.Stream(_controlManager.get());
		serializer.Stream(_spc.get());
		if(_msu1) {
			serializer.Stream(_msu1.get());
		}
	} else {
		serializer.Stream(_cart.get());
		serializer.Stream(_controlManager.get());
	}
	_notificationManager->SendNotification(ConsoleNotificationType::StateLoaded);
}

shared_ptr<SoundMixer> Console::GetSoundMixer()
{
	return _soundMixer;
}

shared_ptr<VideoRenderer> Console::GetVideoRenderer()
{
	return _videoRenderer;
}

shared_ptr<VideoDecoder> Console::GetVideoDecoder()
{
	return _videoDecoder;
}

shared_ptr<NotificationManager> Console::GetNotificationManager()
{
	return _notificationManager;
}

shared_ptr<EmuSettings> Console::GetSettings()
{
	return _settings;
}

shared_ptr<SaveStateManager> Console::GetSaveStateManager()
{
	return _saveStateManager;
}

shared_ptr<RewindManager> Console::GetRewindManager()
{
	return _rewindManager;
}

shared_ptr<DebugHud> Console::GetDebugHud()
{
	return _debugHud;
}

shared_ptr<BatteryManager> Console::GetBatteryManager()
{
	return _batteryManager;
}

shared_ptr<CheatManager> Console::GetCheatManager()
{
	return _cheatManager;
}

shared_ptr<MovieManager> Console::GetMovieManager()
{
	return _movieManager;
}

shared_ptr<Cpu> Console::GetCpu()
{
	return _cpu;
}

shared_ptr<Ppu> Console::GetPpu()
{
	return _ppu;
}

shared_ptr<Spc> Console::GetSpc()
{
	return _spc;
}

shared_ptr<BaseCartridge> Console::GetCartridge()
{
	return _cart;
}

shared_ptr<MemoryManager> Console::GetMemoryManager()
{
	return _memoryManager;
}

shared_ptr<InternalRegisters> Console::GetInternalRegisters()
{
	return _internalRegisters;
}

shared_ptr<ControlManager> Console::GetControlManager()
{
	return _controlManager;
}

shared_ptr<DmaController> Console::GetDmaController()
{
	return _dmaController;
}

shared_ptr<Msu1> Console::GetMsu1()
{
	return _msu1;
}

bool Console::IsRunning()
{
	return _cpu != nullptr;
}

uint32_t Console::GetFrameCount()
{
	shared_ptr<BaseCartridge> cart = _cart;
	if(_settings->CheckFlag(EmulationFlags::GameboyMode) && cart->GetGameboy()) {
		GbPpu* ppu = cart->GetGameboy()->GetPpu();
		return ppu ? ppu->GetFrameCount() : 0;
	} else {
		shared_ptr<Ppu> ppu = _ppu;
		return ppu ? ppu->GetFrameCount() : 0;
	}
}

template<CpuType type>
void Console::ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi)
{
}

void Console::ProcessEvent(EventType type)
{
}

void Console::BreakImmediately(BreakSource source)
{
}

template void Console::ProcessMemoryRead<CpuType::Cpu>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::Sa1>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::Spc>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::Gsu>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::NecDsp>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::Cx4>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryRead<CpuType::Gameboy>(uint32_t addr, uint8_t value, MemoryOperationType opType);

template void Console::ProcessMemoryWrite<CpuType::Cpu>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::Sa1>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::Spc>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::Gsu>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::NecDsp>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::Cx4>(uint32_t addr, uint8_t value, MemoryOperationType opType);
template void Console::ProcessMemoryWrite<CpuType::Gameboy>(uint32_t addr, uint8_t value, MemoryOperationType opType);

template void Console::ProcessInterrupt<CpuType::Cpu>(uint32_t originalPc, uint32_t currentPc, bool forNmi);
template void Console::ProcessInterrupt<CpuType::Sa1>(uint32_t originalPc, uint32_t currentPc, bool forNmi);
template void Console::ProcessInterrupt<CpuType::Gameboy>(uint32_t originalPc, uint32_t currentPc, bool forNmi);
