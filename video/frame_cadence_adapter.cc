/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/frame_cadence_adapter.h"

#include <memory>
#include <utility>

#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "rtc_base/logging.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_utils/pending_task_safety_flag.h"
#include "rtc_base/task_utils/to_queued_task.h"
#include "system_wrappers/include/field_trial.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {
namespace {

class FrameCadenceAdapterImpl : public FrameCadenceAdapterInterface {
 public:
  explicit FrameCadenceAdapterImpl(TaskQueueBase* worker_queue);

  // FrameCadenceAdapterInterface overrides.
  void Initialize(Callback* callback) override;
  void SetZeroHertzModeEnabled(bool enabled) override;

  // VideoFrameSink overrides.
  void OnFrame(const VideoFrame& frame) override;
  void OnDiscardedFrame() override { callback_->OnDiscardedFrame(); }
  void OnConstraintsChanged(
      const VideoTrackSourceConstraints& constraints) override;

 private:
  // Called from OnFrame in zero-hertz mode.
  void OnFrameOnMainQueue(const VideoFrame& frame) RTC_RUN_ON(worker_queue_);

  // Called to report on constraint UMAs.
  void MaybeReportFrameRateConstraintUmas() RTC_RUN_ON(&worker_queue_);

  TaskQueueBase* const worker_queue_;

  // True if we support frame entry for screenshare with a minimum frequency of
  // 0 Hz.
  const bool zero_hertz_screenshare_enabled_;

  // Set up during Initialize.
  Callback* callback_ = nullptr;

  // The source's constraints.
  absl::optional<VideoTrackSourceConstraints> source_constraints_
      RTC_GUARDED_BY(worker_queue_);

  // Whether zero-hertz and UMA reporting is enabled.
  bool zero_hertz_and_uma_reporting_enabled_ RTC_GUARDED_BY(worker_queue_) =
      false;

  // Race checker for incoming frames. This is the network thread in chromium,
  // but may vary from test contexts.
  rtc::RaceChecker incoming_frame_race_checker_;
  bool has_reported_screenshare_frame_rate_umas_ RTC_GUARDED_BY(worker_queue_) =
      false;

  ScopedTaskSafety safety_;
};

FrameCadenceAdapterImpl::FrameCadenceAdapterImpl(TaskQueueBase* worker_queue)
    : worker_queue_(worker_queue),
      zero_hertz_screenshare_enabled_(
          field_trial::IsEnabled("WebRTC-ZeroHertzScreenshare")) {
  RTC_DCHECK_RUN_ON(worker_queue_);
}

void FrameCadenceAdapterImpl::Initialize(Callback* callback) {
  callback_ = callback;
}

void FrameCadenceAdapterImpl::SetZeroHertzModeEnabled(bool enabled) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (enabled && !zero_hertz_and_uma_reporting_enabled_)
    has_reported_screenshare_frame_rate_umas_ = false;
  zero_hertz_and_uma_reporting_enabled_ = enabled;
}

void FrameCadenceAdapterImpl::OnFrame(const VideoFrame& frame) {
  // This method is called on the network thread under Chromium, or other
  // various contexts in test.
  RTC_DCHECK_RUNS_SERIALIZED(&incoming_frame_race_checker_);
  worker_queue_->PostTask(ToQueuedTask(safety_, [this, frame] {
    RTC_DCHECK_RUN_ON(worker_queue_);
    OnFrameOnMainQueue(std::move(frame));
    MaybeReportFrameRateConstraintUmas();
  }));
}

void FrameCadenceAdapterImpl::OnConstraintsChanged(
    const VideoTrackSourceConstraints& constraints) {
  RTC_LOG(LS_INFO) << __func__ << " min_fps "
                   << constraints.min_fps.value_or(-1) << " max_fps "
                   << constraints.max_fps.value_or(-1);
  worker_queue_->PostTask(ToQueuedTask(safety_, [this, constraints] {
    RTC_DCHECK_RUN_ON(worker_queue_);
    source_constraints_ = constraints;
  }));
}

// RTC_RUN_ON(worker_queue_)
void FrameCadenceAdapterImpl::OnFrameOnMainQueue(const VideoFrame& frame) {
  callback_->OnFrame(frame);
}

// RTC_RUN_ON(worker_queue_)
void FrameCadenceAdapterImpl::MaybeReportFrameRateConstraintUmas() {
  if (has_reported_screenshare_frame_rate_umas_)
    return;
  has_reported_screenshare_frame_rate_umas_ = true;
  if (!zero_hertz_and_uma_reporting_enabled_)
    return;
  RTC_HISTOGRAM_BOOLEAN("WebRTC.Screenshare.FrameRateConstraints.Exists",
                        source_constraints_.has_value());
  if (!source_constraints_.has_value())
    return;
  RTC_HISTOGRAM_BOOLEAN("WebRTC.Screenshare.FrameRateConstraints.Min.Exists",
                        source_constraints_->min_fps.has_value());
  if (source_constraints_->min_fps.has_value()) {
    RTC_HISTOGRAM_COUNTS_100(
        "WebRTC.Screenshare.FrameRateConstraints.Min.Value",
        source_constraints_->min_fps.value());
  }
  RTC_HISTOGRAM_BOOLEAN("WebRTC.Screenshare.FrameRateConstraints.Max.Exists",
                        source_constraints_->max_fps.has_value());
  if (source_constraints_->max_fps.has_value()) {
    RTC_HISTOGRAM_COUNTS_100(
        "WebRTC.Screenshare.FrameRateConstraints.Max.Value",
        source_constraints_->max_fps.value());
  }
  if (!source_constraints_->min_fps.has_value()) {
    if (source_constraints_->max_fps.has_value()) {
      RTC_HISTOGRAM_COUNTS_100(
          "WebRTC.Screenshare.FrameRateConstraints.MinUnset.Max",
          source_constraints_->max_fps.value());
    }
  } else if (source_constraints_->max_fps.has_value()) {
    if (source_constraints_->min_fps.value() <
        source_constraints_->max_fps.value()) {
      RTC_HISTOGRAM_COUNTS_100(
          "WebRTC.Screenshare.FrameRateConstraints.MinLessThanMax.Min",
          source_constraints_->min_fps.value());
      RTC_HISTOGRAM_COUNTS_100(
          "WebRTC.Screenshare.FrameRateConstraints.MinLessThanMax.Max",
          source_constraints_->max_fps.value());
    }
    // Multi-dimensional histogram for min and max FPS making it possible to
    // uncover min and max combinations. See
    // https://chromium.googlesource.com/chromium/src.git/+/HEAD/tools/metrics/histograms/README.md#multidimensional-histograms
    constexpr int kMaxBucketCount =
        60 * /*max min_fps=*/60 + /*max max_fps=*/60 - 1;
    RTC_HISTOGRAM_ENUMERATION_SPARSE(
        "WebRTC.Screenshare.FrameRateConstraints.60MinPlusMaxMinusOne",
        source_constraints_->min_fps.value() * 60 +
            source_constraints_->max_fps.value() - 1,
        /*boundary=*/kMaxBucketCount);
  }
}

}  // namespace

std::unique_ptr<FrameCadenceAdapterInterface>
FrameCadenceAdapterInterface::Create(TaskQueueBase* worker_queue) {
  return std::make_unique<FrameCadenceAdapterImpl>(worker_queue);
}

}  // namespace webrtc
