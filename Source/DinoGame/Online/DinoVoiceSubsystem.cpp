#include "Online/DinoVoiceSubsystem.h"

#include "DinoGame.h"
#include "DinoPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/VoiceCapture.h"
#include "Interfaces/VoiceInterface.h"
#include "Net/VoiceConfig.h"
#include "Online/DinoVoiceTalker.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "VoiceEngineImpl.h"
#include "VoiceInterfaceImpl.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <dsound.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	/** Sensitivity 0..1 maps onto this range of voice.SilenceDetectionThreshold (loud .. quiet). */
	constexpr float ThresholdAtNoSensitivity = 0.06f;
	constexpr float ThresholdAtFullSensitivity = 0.004f;

	/** The noise gate sits below the talking threshold, so the start of a word is not clipped. */
	constexpr float NoiseGateFraction = 0.5f;

	constexpr float TickInterval = 0.1f;
	constexpr double PeakWindowSeconds = 3.0;
	constexpr double HealthCheckInterval = 1.0;

	/** Windows sends several notifications for one plug or unplug. Act once they stop. */
	constexpr double DeviceSettleSeconds = 0.75;

	/**
	 * Reaches the voice engine's mic object, which Unreal keeps protected inside the voice
	 * interface and the voice engine. Both steps use the standard C++ route to a protected
	 * member - naming it through a derived class - and read nothing but that member.
	 *
	 * Engine-version specific: written against 5.8.1's FOnlineVoiceImpl::VoiceEngine and
	 * FVoiceEngineImpl::GetVoiceCapture. Steam (FOnlineVoiceSteam) and Null (play in editor) both
	 * use these classes. Any other online backend gets null here, and mic recovery just stays off.
	 */
	struct FVoiceInterfaceAccess : public FOnlineVoiceImpl
	{
		static IVoiceEnginePtr& Engine(FOnlineVoiceImpl& Voice)
		{
			return Voice.*(&FVoiceInterfaceAccess::VoiceEngine);
		}
	};

	struct FVoiceEngineAccess : public FVoiceEngineImpl
	{
		static TSharedPtr<IVoiceCapture>& Capture(FVoiceEngineImpl& VoiceEngine)
		{
			using FGetter = TSharedPtr<IVoiceCapture>& (FVoiceEngineImpl::*)();
			const FGetter Getter = &FVoiceEngineAccess::GetVoiceCapture;
			return (VoiceEngine.*Getter)();
		}
	};

	/** The voice engine behind the voice interface, or null before voice starts or on another backend. */
	IVoiceEnginePtr GetVoiceEngine()
	{
		IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();
		if (!Subsystem)
		{
			return nullptr;
		}

		const FName Name = Subsystem->GetSubsystemName();
		if (Name != STEAM_SUBSYSTEM && Name != NULL_SUBSYSTEM)
		{
			return nullptr;
		}

		const IOnlineVoicePtr Voice = Subsystem->GetVoiceInterface();
		if (!Voice.IsValid())
		{
			return nullptr;
		}

		return FVoiceInterfaceAccess::Engine(*static_cast<FOnlineVoiceImpl*>(Voice.Get()));
	}

	TSharedPtr<IVoiceCapture> GetEngineMicCapture()
	{
		const IVoiceEnginePtr Engine = GetVoiceEngine();
		return Engine.IsValid() ? FVoiceEngineAccess::Capture(*static_cast<FVoiceEngineImpl*>(Engine.Get())) : nullptr;
	}

#if PLATFORM_WINDOWS
	BOOL CALLBACK CollectMicName(LPGUID Guid, LPCWSTR Description, LPCWSTR /*Module*/, LPVOID Context)
	{
		// The first entry has no GUID: it is "Primary Sound Capture Driver", an alias rather than
		// a device. The engine skips it the same way, so the names here match the engine's list.
		if (Guid)
		{
			static_cast<TArray<FString>*>(Context)->Add(FString(Description));
		}
		return 1; // TRUE: keep enumerating
	}
#endif

	/**
	 * Mic names as the engine knows them, the Windows default input first. Enumerated exactly as
	 * FVoiceCaptureDeviceWindows does it (DirectSound), so a name from here is one ChangeDevice
	 * accepts.
	 */
	TArray<FString> EnumerateMics()
	{
		TArray<FString> Names;
#if PLATFORM_WINDOWS
		DirectSoundCaptureEnumerateW(&CollectMicName, &Names);
#endif
		return Names;
	}
}

#if PLATFORM_WINDOWS
/**
 * Windows Core Audio device notifications. Called on a Windows thread, so it does nothing but
 * set a flag the game thread polls. Reference counted, as COM requires; created with one
 * reference, which Stop releases.
 */
class FDinoMicWatcher final : public IMMNotificationClient
{
public:
	FThreadSafeBool bChanged;

	bool Start()
	{
		const HRESULT Result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&Enumerator));
		if (FAILED(Result) || !Enumerator)
		{
			Enumerator = nullptr;
			return false;
		}
		return SUCCEEDED(Enumerator->RegisterEndpointNotificationCallback(this));
	}

	void Stop()
	{
		if (Enumerator)
		{
			Enumerator->UnregisterEndpointNotificationCallback(this);
			Enumerator->Release();
			Enumerator = nullptr;
		}
		Release();
	}

	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(FPlatformAtomics::InterlockedIncrement(&RefCount));
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const int32 Remaining = FPlatformAtomics::InterlockedDecrement(&RefCount);
		if (Remaining == 0)
		{
			delete this;
		}
		return static_cast<ULONG>(Remaining);
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void** OutInterface) override
	{
		if (Riid == __uuidof(IUnknown) || Riid == __uuidof(IMMNotificationClient))
		{
			*OutInterface = static_cast<IMMNotificationClient*>(this);
			AddRef();
			return S_OK;
		}
		*OutInterface = nullptr;
		return E_NOINTERFACE;
	}

	// IMMNotificationClient. Device events cannot tell an input from an output without a
	// lookup, so all of them count; the mic list is re-read only once they settle.
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { bChanged = true; return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { bChanged = true; return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { bChanged = true; return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow Flow, ERole, LPCWSTR) override
	{
		if (Flow == eCapture || Flow == eAll)
		{
			bChanged = true;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
	int32 RefCount = 1;
	IMMDeviceEnumerator* Enumerator = nullptr;
};
#endif

// --- Lifetime --------------------------------------------------------------------------------

void UDinoVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The same moment, near enough, that the engine took its own list - so this matches the
	// mics it can open.
	StartupMics = EnumerateMics();
	UE_LOG(LogDinoNet, Log, TEXT("Microphones at startup: %s"),
		StartupMics.IsEmpty() ? TEXT("none") : *FString::Join(StartupMics, TEXT(", ")));

	ApplyMicSensitivity();

#if PLATFORM_WINDOWS
	Watcher = new FDinoMicWatcher();
	if (!Watcher->Start())
	{
		UE_LOG(LogDinoNet, Warning, TEXT("Microphone: could not watch Windows for device changes; an unplugged mic will not reconnect."));
	}
#endif

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDinoVoiceSubsystem::Tick), TickInterval);
}

void UDinoVoiceSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);

#if PLATFORM_WINDOWS
	if (Watcher)
	{
		Watcher->Stop();
		Watcher = nullptr;
	}
#endif

	Super::Deinitialize();
}

bool UDinoVoiceSubsystem::Tick(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();

	// Mic level, for the Tab menu meter and DinoVoiceStatus.
	if (const IVoiceEnginePtr Engine = GetVoiceEngine())
	{
		// -1 while there is no mic open.
		LevelSamples.Add({ Now, FMath::Max(0.0f, Engine->GetMicrophoneAmplitude(0)) });
	}
	LevelSamples.RemoveAll([Now](const FLevelSample& Sample) { return Now - Sample.Time > PeakWindowSeconds; });

#if PLATFORM_WINDOWS
	if (Watcher && Watcher->bChanged)
	{
		Watcher->bChanged = false;
		DevicesChangedAt = Now;
	}
#endif

	if (DevicesChangedAt >= 0.0 && Now - DevicesChangedAt >= DeviceSettleSeconds)
	{
		DevicesChangedAt = -1.0;
		HandleMicDevicesChanged();
	}

	if (Now >= NextHealthCheck)
	{
		NextHealthCheck = Now + HealthCheckInterval;

		const TSharedPtr<IVoiceCapture> Capture = GetEngineMicCapture();

		// The engine opens its mic the first time voice starts (hosting or joining), on the mic
		// that was the default when the game started. Check straight away whether that is
		// still the one that should be open.
		if (Capture.IsValid() && Capture != SeenCapture.Pin())
		{
			SeenCapture = Capture;
			ActiveMic = StartupMics.IsEmpty() ? FString() : StartupMics[0];
			bMicLost = false;
			HandleMicDevicesChanged();
		}

		uint32 Unused = 0;
		if (Capture.IsValid() && !bMicLost && Capture->GetCaptureState(Unused) == EVoiceCaptureState::Error)
		{
			// Unplugged, almost always. The engine logs it once and then never captures again.
			bMicLost = true;
			UE_LOG(LogDinoNet, Warning, TEXT("Microphone: %s stopped working."), *ActiveMic);
			HandleMicDevicesChanged();
		}
	}

	return true;
}

// --- Per-player ------------------------------------------------------------------------------

float UDinoVoiceSubsystem::GetPlayerVolume(const FUniqueNetIdRepl& PlayerId) const
{
	const float* Volume = PlayerId.IsValid() ? PlayerVolumes.Find(PlayerId.ToString()) : nullptr;
	return Volume ? *Volume : 1.0f;
}

void UDinoVoiceSubsystem::SetPlayerVolume(ADinoPlayerState* Player, float Volume)
{
	if (!Player || !Player->GetUniqueId().IsValid())
	{
		return;
	}

	PlayerVolumes.Add(Player->GetUniqueId().ToString(), FMath::Clamp(Volume, 0.0f, 2.0f));

	if (UDinoVoiceTalker* Talker = Player->GetVoiceTalker())
	{
		Talker->ApplyLocalVolume();
	}
}

bool UDinoVoiceSubsystem::IsPlayerMuted(const FUniqueNetIdRepl& PlayerId) const
{
	return PlayerId.IsValid() && MutedPlayers.Contains(PlayerId.ToString());
}

void UDinoVoiceSubsystem::SetPlayerMuted(APlayerController* LocalController, ADinoPlayerState* Player, bool bMuted)
{
	if (!LocalController || !Player || !Player->GetUniqueId().IsValid())
	{
		return;
	}

	const FUniqueNetIdRepl& Id = Player->GetUniqueId();
	if (bMuted)
	{
		MutedPlayers.Add(Id.ToString());
		LocalController->ServerMutePlayer(Id);
	}
	else
	{
		MutedPlayers.Remove(Id.ToString());
		LocalController->ServerUnmutePlayer(Id);
	}
}

void UDinoVoiceSubsystem::ReapplyMutes(APlayerController* LocalController) const
{
	const UWorld* World = LocalController ? LocalController->GetWorld() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState || MutedPlayers.IsEmpty())
	{
		return;
	}

	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (Player && Player->GetUniqueId().IsValid() && MutedPlayers.Contains(Player->GetUniqueId().ToString()))
		{
			LocalController->ServerMutePlayer(Player->GetUniqueId());
		}
	}
}

// --- Mic sensitivity -------------------------------------------------------------------------

float UDinoVoiceSubsystem::GetMicThreshold() const
{
	const IConsoleVariable* Threshold = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.SilenceDetectionThreshold"));
	return Threshold ? Threshold->GetFloat() : 0.0f;
}

float UDinoVoiceSubsystem::GetMicSensitivity() const
{
	if (MicSensitivity >= 0.0f)
	{
		return MicSensitivity;
	}

	// Never set: show where the ini's threshold falls on the same scale.
	return ThresholdToSensitivity(GetMicThreshold());
}

float UDinoVoiceSubsystem::SensitivityToThreshold(float Sensitivity)
{
	return FMath::Lerp(ThresholdAtNoSensitivity, ThresholdAtFullSensitivity, FMath::Clamp(Sensitivity, 0.0f, 1.0f));
}

float UDinoVoiceSubsystem::ThresholdToSensitivity(float Threshold)
{
	return FMath::Clamp((ThresholdAtNoSensitivity - Threshold) / (ThresholdAtNoSensitivity - ThresholdAtFullSensitivity), 0.0f, 1.0f);
}

void UDinoVoiceSubsystem::SetMicSensitivity(float Sensitivity)
{
	MicSensitivity = FMath::Clamp(Sensitivity, 0.0f, 1.0f);
	ApplyMicSensitivity();
	SaveConfig();
}

void UDinoVoiceSubsystem::ApplyMicSensitivity() const
{
	if (MicSensitivity < 0.0f)
	{
		return;
	}

	const float Threshold = SensitivityToThreshold(MicSensitivity);

	// At code priority, not the "game setting" priority UVOIPStatics::SetMicThreshold uses:
	// these two are set in DefaultEngine.ini [SystemSettings], which outranks game settings, so
	// that call is silently ignored. The capture reads both values live.
	if (IConsoleVariable* Silence = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.SilenceDetectionThreshold")))
	{
		Silence->Set(Threshold, ECVF_SetByCode);
	}
	if (IConsoleVariable* Gate = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicNoiseGateThreshold")))
	{
		Gate->Set(Threshold * NoiseGateFraction, ECVF_SetByCode);
	}
}

float UDinoVoiceSubsystem::GetRecentMicPeak() const
{
	float Peak = 0.0f;
	for (const FLevelSample& Sample : LevelSamples)
	{
		Peak = FMath::Max(Peak, Sample.Level);
	}
	return Peak;
}

// --- Mic device ------------------------------------------------------------------------------

void UDinoVoiceSubsystem::PickMic(const FString& MicName)
{
	PickedMic = MicName;
	SaveConfig();
	HandleMicDevicesChanged();
}

void UDinoVoiceSubsystem::RefreshMic()
{
	if (!GetEngineMicCapture().IsValid())
	{
		DinoScreenLog(TEXT("Microphone: voice has not started yet - host or join first."), FColor(240, 200, 80), 5.0f);
		return;
	}

	// Treat it as lost so the mic is reopened even if it looks like the right one already.
	bMicLost = true;
	HandleMicDevicesChanged();
}

void UDinoVoiceSubsystem::HandleMicDevicesChanged()
{
	const TArray<FString> CurrentMics = EnumerateMics();

	const TSharedPtr<IVoiceCapture> Capture = GetEngineMicCapture();
	if (!Capture.IsValid())
	{
		// Voice has not started yet (main menu), or never could. The mic is chosen when it does.
		return;
	}

	// The picked mic if it is plugged in, otherwise whatever Windows now calls the default.
	const FString Wanted = !PickedMic.IsEmpty() && CurrentMics.Contains(PickedMic)
		? PickedMic
		: (CurrentMics.IsEmpty() ? FString() : CurrentMics[0]);

	if (Wanted.IsEmpty())
	{
		if (!bMissingMicNotified)
		{
			bMissingMicNotified = true;
			DinoScreenError(TEXT("No microphone connected. Plug one in and voice will come back."));
		}
		return;
	}
	bMissingMicNotified = false;

	if (!StartupMics.Contains(Wanted))
	{
		// The engine's mic list was read at startup and cannot be refreshed, so it has no way to
		// open this one. Keep whatever is working and say why the new one is not used.
		NewMicNeedingRestart = Wanted;
		if (!NewMicsNotified.Contains(Wanted))
		{
			NewMicsNotified.Add(Wanted);
			DinoScreenLog(FString::Printf(TEXT("New microphone \"%s\" found. Restart the game to use it."), *Wanted),
				FColor(240, 200, 80), 10.0f);
		}
		return;
	}

	NewMicNeedingRestart.Reset();

	if (Wanted == ActiveMic && !bMicLost)
	{
		return;
	}

	OpenMic(Wanted, *Capture);
}

bool UDinoVoiceSubsystem::OpenMic(const FString& MicName, IVoiceCapture& Capture)
{
	// After a failure the capture has stopped; otherwise keep it in whichever state voice had it.
	const bool bShouldCapture = bMicLost || Capture.IsCapturing();

	if (!Capture.ChangeDevice(MicName, UVOIPStatics::GetVoiceSampleRate(), UVOIPStatics::GetVoiceNumChannels()))
	{
		UE_LOG(LogDinoNet, Warning, TEXT("Microphone: could not open %s."), *MicName);
		return false;
	}

	// ChangeDevice builds a fresh capture buffer, stopped.
	if (bShouldCapture)
	{
		Capture.Start();
	}

	ActiveMic = MicName;
	bMicLost = false;
	DinoScreenLog(FString::Printf(TEXT("Microphone: %s"), *MicName), FColor(120, 220, 120), 5.0f);
	return true;
}

void UDinoVoiceSubsystem::ShowStartupNotices()
{
	if (bStartupNoticesShown)
	{
		return;
	}
	bStartupNoticesShown = true;

	if (StartupMics.IsEmpty())
	{
		DinoScreenError(TEXT("No microphone found. Connect one, then restart the game to use voice chat."));
	}
}

FString UDinoVoiceSubsystem::GetMicNotice() const
{
	if (StartupMics.IsEmpty())
	{
		return TEXT("No microphone was connected when the game started. Connect one and restart to talk.");
	}
	if (bMicLost)
	{
		return TEXT("Your microphone stopped working. Plug it back in, or try Reconnect mic.");
	}
	if (!NewMicNeedingRestart.IsEmpty())
	{
		return FString::Printf(TEXT("\"%s\" was connected after the game started. Restart the game to use it."), *NewMicNeedingRestart);
	}
	return FString();
}

void UDinoVoiceSubsystem::DescribeMic(TArray<FString>& OutLines) const
{
	const TSharedPtr<IVoiceCapture> Capture = GetEngineMicCapture();
	uint32 Unused = 0;

	OutLines.Add(FString::Printf(TEXT("  mic in use      : %s%s"),
		ActiveMic.IsEmpty() ? TEXT("<voice not started>") : *ActiveMic,
		bMicLost ? TEXT("  (LOST - plug it back in)") : TEXT("")));
	OutLines.Add(FString::Printf(TEXT("  mic picked      : %s"), PickedMic.IsEmpty() ? TEXT("Windows default") : *PickedMic));
	OutLines.Add(FString::Printf(TEXT("  capture state   : %s"),
		Capture.IsValid() ? EVoiceCaptureState::ToString(Capture->GetCaptureState(Unused)) : TEXT("<none>")));
	OutLines.Add(FString::Printf(TEXT("  mic peak (3s)   : %.3f   sent above %.3f"), GetRecentMicPeak(), GetMicThreshold()));
	OutLines.Add(FString::Printf(TEXT("  mics at startup : %s"),
		StartupMics.IsEmpty() ? TEXT("none") : *FString::Join(StartupMics, TEXT(", "))));
}
