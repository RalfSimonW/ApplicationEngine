#pragma once

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>

#ifndef RESIZE_DIAG_ENABLED
#define RESIZE_DIAG_ENABLED 1
#endif

#if RESIZE_DIAG_ENABLED
namespace ResizeDiag
{
	inline std::mutex logMutex;

	// Existing lifetime counters
	inline std::atomic<unsigned long long> wmSizeCount{ 0 };
	inline std::atomic<unsigned long long> resizeBuffersCount{ 0 };
	inline std::atomic<unsigned long long> rtvCreateCount{ 0 };
	inline std::atomic<unsigned long long> presentCount{ 0 };
	inline std::atomic<unsigned long long> presentFailureCount{ 0 };

	inline std::atomic<unsigned long long> mainRtvCreateCount{ 0 };
	inline std::atomic<unsigned long long> sceneRtvCreateCount{ 0 };

	// New resize-duration measurements.
	// These will be reset at the start of each interactive resize.
	inline std::atomic<unsigned long long> resizeBuffersSuccessCount{ 0 };
	inline std::atomic<unsigned long long> resizeBuffersTimedCount{ 0 };
	inline std::atomic<unsigned long long> resizeBuffersTotalUs{ 0 };
	inline std::atomic<unsigned long long> resizeBuffersMaxUs{ 0 };

	inline std::atomic<unsigned long long> presentTimedCount{ 0 };
	inline std::atomic<unsigned long long> presentTimeUs{ 0 };
	inline std::atomic<unsigned long long> presentMaxUs{ 0 };

	inline std::atomic<unsigned long long> frameTimedCount{ 0 };
	inline std::atomic<unsigned long long> frameTimeUs{ 0 };
	inline std::atomic<unsigned long long> frameMaxUs{ 0 };

	inline std::atomic<unsigned long long> frameWorkTotalUs{ 0 };
	inline std::atomic<unsigned long long> frameWorkMaxUs{ 0 };

	inline std::atomic<unsigned long long> sceneApplyCount{ 0 };
	inline std::atomic<unsigned long long> sceneApplyTotalUs{ 0 };
	inline std::atomic<unsigned long long> sceneApplyMaxUs{ 0 };

	// Packed size that the render thread has finished applying.
	inline std::atomic<uint64_t> mainResizeAppliedSize{ 0 };

	// Only read/written on the render thread.
	inline uint64_t sceneTextureGeneration = 0;

	inline std::mutex measurementMutex;
	inline std::atomic<bool> measurementActive{ false };
	inline std::atomic<uint64_t> measurementSessionId{ 0 };
	inline std::chrono::steady_clock::time_point measurementStart;
	inline uint64_t measurementFrameNumber = 0; // protected by measurementMutex


	inline void Log(const char* format, ...)
	{
		char message[512];

		va_list args;
		va_start(args, format);
		vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
		va_end(args);

		char line[640];
		_snprintf_s(
			line,
			sizeof(line),
			_TRUNCATE,
			"[ResizeDiag %llu ms TID %lu] %s\n",
			static_cast<unsigned long long>(GetTickCount64()),
			GetCurrentThreadId(),
			message);

		std::lock_guard<std::mutex> lock(logMutex);
		OutputDebugStringA(line);
	}

	inline void UpdateMax(
		std::atomic<unsigned long long>& maximum,
		unsigned long long value)
	{
		unsigned long long oldValue = maximum.load();

		while (value > oldValue &&
			!maximum.compare_exchange_weak(oldValue, value))
		{
			// Retry if another thread changed maximum.
		}
	}

	inline void ResetResizeMeasurements()
	{
		std::lock_guard<std::mutex> lock(measurementMutex);

		measurementActive.store(false, std::memory_order_release);

		resizeBuffersSuccessCount.store(0);
		resizeBuffersTimedCount.store(0);
		resizeBuffersTotalUs.store(0);
		resizeBuffersMaxUs.store(0);

		presentTimedCount.store(0);
		presentTimeUs.store(0);
		presentMaxUs.store(0);

		frameTimedCount.store(0);
		frameTimeUs.store(0);
		frameMaxUs.store(0);

		frameWorkTotalUs.store(0);
		frameWorkMaxUs.store(0);

		sceneApplyCount.store(0);
		sceneApplyTotalUs.store(0);
		sceneApplyMaxUs.store(0);

		measurementFrameNumber = 0;
		measurementStart = std::chrono::steady_clock::now();

		const uint64_t session =
			measurementSessionId.fetch_add(1, std::memory_order_acq_rel) + 1;

		measurementActive.store(true, std::memory_order_release);

		Log("Resize measurement session #%llu begin",
			static_cast<unsigned long long>(session));
	}

	inline uint64_t ActiveMeasurementSession()
	{
		if (!measurementActive.load(std::memory_order_acquire))
			return 0;

		return measurementSessionId.load(std::memory_order_acquire);
	}

	inline void RecordResizeBuffersTiming(
		uint64_t session,
		bool succeeded,
		unsigned long long durationUs)
	{
		if (session == 0)
			return;

		std::lock_guard<std::mutex> lock(measurementMutex);

		if (!measurementActive.load(std::memory_order_acquire) ||
			session != measurementSessionId.load(std::memory_order_acquire))
		{
			return;
		}

		resizeBuffersTimedCount.fetch_add(1);
		resizeBuffersTotalUs.fetch_add(durationUs);
		UpdateMax(resizeBuffersMaxUs, durationUs);

		if (succeeded)
			resizeBuffersSuccessCount.fetch_add(1);
	}

	inline void RecordFrameTiming(
		uint64_t session,
		unsigned long long frameUs,
		unsigned long long presentUs,
		unsigned long long sceneApplyUs,
		unsigned long long mainResizeUs,
		unsigned long long renderSceneUs,
		unsigned long long imguiBuildUs,
		unsigned long long mainDrawUs,
		unsigned long long platformWindowsUs,
		bool sceneApplyCalled)
	{
		if (session == 0)
			return;

		uint64_t frameNumber = 0;
		unsigned long long outsidePresentUs = 0;
		bool logSlowFrame = false;

		{
			std::lock_guard<std::mutex> lock(measurementMutex);

			if (!measurementActive.load(std::memory_order_acquire) ||
				session != measurementSessionId.load(std::memory_order_acquire))
			{
				return;
			}

			frameNumber = ++measurementFrameNumber;

			frameTimedCount.fetch_add(1);
			frameTimeUs.fetch_add(frameUs);
			UpdateMax(frameMaxUs, frameUs);

			presentTimedCount.fetch_add(1);
			presentTimeUs.fetch_add(presentUs);
			UpdateMax(presentMaxUs, presentUs);

			outsidePresentUs =
				frameUs > presentUs ? frameUs - presentUs : 0;

			frameWorkTotalUs.fetch_add(outsidePresentUs);
			UpdateMax(frameWorkMaxUs, outsidePresentUs);

			if (sceneApplyCalled)
			{
				sceneApplyCount.fetch_add(1);
				sceneApplyTotalUs.fetch_add(sceneApplyUs);
				UpdateMax(sceneApplyMaxUs, sceneApplyUs);
			}

			logSlowFrame = frameUs >= 12000; // 12 ms
		}

		if (logSlowFrame)
		{
			const unsigned long long measuredUs =
				mainResizeUs +
				(sceneApplyCalled ? sceneApplyUs : 0) +
				renderSceneUs +
				imguiBuildUs +
				mainDrawUs +
				platformWindowsUs;

			const unsigned long long otherOutsidePresentUs =
				outsidePresentUs > measuredUs
				? outsidePresentUs - measuredUs
				: 0;

			Log("Resize frame #%llu: frame=%llu us Present=%llu us "
				"outsidePresent=%llu us MainResize=%llu us "
				"ApplySceneResize=%llu us RenderScene=%llu us "
				"ImGuiBuild=%llu us MainDraw=%llu us Viewports=%llu us "
				"other=%llu us",
				static_cast<unsigned long long>(frameNumber),
				frameUs,
				presentUs,
				outsidePresentUs,
				mainResizeUs,
				sceneApplyUs,
				renderSceneUs,
				imguiBuildUs,
				mainDrawUs,
				platformWindowsUs,
				otherOutsidePresentUs);
		}
	}

	inline void RecordFrameTimingMulti(
		uint64_t session,
		unsigned long long frameUs,
		unsigned long long presentUs,
		unsigned long long sceneApplyUs,
		unsigned long long mainResizeUs,
		unsigned long long inputQueueUs,
		unsigned long long gpuQueryPollUs,
		unsigned long long graphicsBeginOtherUs,
		unsigned long long sceneSetupUs,
		unsigned long long renderSceneUs,
		unsigned long long imguiBuildUs,
		unsigned long long mainDrawUs,
		unsigned long long platformWindowsUs,
		unsigned long long viewportCallbacksUs,
		bool sceneApplyCalled)
	{
		if (session == 0)
			return;

		uint64_t frameNumber = 0;
		unsigned long long outsidePresentUs = 0;
		bool logSlowFrame = false;

		{
			std::lock_guard<std::mutex> lock(measurementMutex);

			if (!measurementActive.load(std::memory_order_acquire) ||
				session != measurementSessionId.load(std::memory_order_acquire))
			{
				return;
			}

			frameNumber = ++measurementFrameNumber;

			frameTimedCount.fetch_add(1);
			frameTimeUs.fetch_add(frameUs);
			UpdateMax(frameMaxUs, frameUs);

			presentTimedCount.fetch_add(1);
			presentTimeUs.fetch_add(presentUs);
			UpdateMax(presentMaxUs, presentUs);

			outsidePresentUs =
				frameUs > presentUs ? frameUs - presentUs : 0;

			frameWorkTotalUs.fetch_add(outsidePresentUs);
			UpdateMax(frameWorkMaxUs, outsidePresentUs);

			if (sceneApplyCalled)
			{
				sceneApplyCount.fetch_add(1);
				sceneApplyTotalUs.fetch_add(sceneApplyUs);
				UpdateMax(sceneApplyMaxUs, sceneApplyUs);
			}

			logSlowFrame = frameUs >= 12000; // 12 ms
		}

		if (logSlowFrame)
		{
			const unsigned long long measuredUs =
				inputQueueUs +
				gpuQueryPollUs +
				mainResizeUs +
				(sceneApplyCalled ? sceneApplyUs : 0) +
				graphicsBeginOtherUs +
				sceneSetupUs +
				renderSceneUs +
				imguiBuildUs +
				mainDrawUs +
				platformWindowsUs +
				viewportCallbacksUs;

			const unsigned long long otherOutsidePresentUs =
				outsidePresentUs > measuredUs
				? outsidePresentUs - measuredUs
				: 0;

			Log(
				"Resize frame #%llu: frame=%llu us Present=%llu us "
				"outsidePresent=%llu us InputQueue=%llu us "
				"GpuQueryPoll=%llu us MainResize=%llu us "
				"ApplySceneResize=%llu us GraphicsBeginOther=%llu us "
				"SceneSetup=%llu us RenderScene=%llu us "
				"ImGuiBuild=%llu us MainDraw=%llu us Viewports=%llu us "
				"ViewportCallbacks=%llu us other=%llu us",
				static_cast<unsigned long long>(frameNumber),
				frameUs,
				presentUs,
				outsidePresentUs,
				inputQueueUs,
				gpuQueryPollUs,       // This argument was missing.
				mainResizeUs,
				sceneApplyUs,
				graphicsBeginOtherUs,
				sceneSetupUs,
				renderSceneUs,
				imguiBuildUs,
				mainDrawUs,
				platformWindowsUs,
				viewportCallbacksUs,
				otherOutsidePresentUs);
		}
	}

	inline void LogStats()
	{
		Log(
			"totals: WM_SIZE=%llu ResizeBuffers=%llu RTV_create=%llu "
			"Present=%llu Present_failures=%llu main_RTV_Create=%llu "
			"scene_RTV_Create=%llu",
			wmSizeCount.load(),
			resizeBuffersCount.load(),
			rtvCreateCount.load(),
			presentCount.load(),
			presentFailureCount.load(),
			mainRtvCreateCount.load(),
			sceneRtvCreateCount.load());

		const unsigned long long resizeCalls =
			resizeBuffersTimedCount.load();
		const unsigned long long resizeTotalUs =
			resizeBuffersTotalUs.load();

		const unsigned long long presentCalls =
			presentTimedCount.load();
		const unsigned long long presentTotalUs =
			presentTimeUs.load();

		Log(
			"resize timings: ResizeBuffers successes=%llu calls=%llu "
			"avg=%llu us max=%llu us; Present calls=%llu avg=%llu us "
			"max=%llu us",
			resizeBuffersSuccessCount.load(),
			resizeCalls,
			resizeCalls ? resizeTotalUs / resizeCalls : 0,
			resizeBuffersMaxUs.load(),
			presentCalls,
			presentCalls ? presentTotalUs / presentCalls : 0,
			presentMaxUs.load());

		const unsigned long long frameCalls =
			frameTimedCount.load();
		const unsigned long long frameTotalUs =
			frameTimeUs.load();
		const unsigned long long frameWorkTotal =
			frameWorkTotalUs.load();

		Log(
			"frame timings: frames=%llu avg=%llu us max=%llu us; "
			"outside main Present avg=%llu us max=%llu us",
			frameCalls,
			frameCalls ? frameTotalUs / frameCalls : 0,
			frameMaxUs.load(),
			frameCalls ? frameWorkTotal / frameCalls : 0,
			frameWorkMaxUs.load());

		const unsigned long long sceneApplyCalls =
			sceneApplyCount.load();
		const unsigned long long sceneApplyTotal =
			sceneApplyTotalUs.load();

		Log(
			"ApplySceneResize: calls=%llu avg=%llu us max=%llu us",
			sceneApplyCalls,
			sceneApplyCalls ? sceneApplyTotal / sceneApplyCalls : 0,
			sceneApplyMaxUs.load());
	}

	inline void EndResizeMeasurements(const char* reason)
	{
		uint64_t session = 0;
		long long elapsedMs = 0;

		{
			std::lock_guard<std::mutex> lock(measurementMutex);

			if (!measurementActive.load(std::memory_order_acquire))
				return;

			session = measurementSessionId.load(std::memory_order_acquire);
			elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - measurementStart).count();

			// RecordFrameTiming and RecordResizeBuffersTiming use the same mutex,
			// so no measurement update can still be in progress after this.
			measurementActive.store(false, std::memory_order_release);
		}

		Log("Resize measurement session #%llu end reason=%s elapsed=%lld ms",
			static_cast<unsigned long long>(session),
			reason ? reason : "unspecified",
			elapsedMs);

		LogStats();
	}

	inline void TraceSize(const char* stage, HWND hwnd, UINT width, UINT height)
	{
		char line[256];
		sprintf_s(
			line,
			"[ResizeDiag %llu ms TID %lu] %s hwnd=%p size=%ux%u\n",
			GetTickCount64(),
			GetCurrentThreadId(),
			stage,
			hwnd,
			width,
			height);

		OutputDebugStringA(line);
	}
}
#endif