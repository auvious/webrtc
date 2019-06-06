/*
 *  Copyright 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import android.content.Context;
import android.support.annotation.Nullable;

public class Camera1Capturer extends CameraCapturer {
  private final boolean captureToTexture;
	private Camera1Session camera1Session;

  public Camera1Capturer(
      String cameraName, CameraEventsHandler eventsHandler, boolean captureToTexture) {
    super(cameraName, eventsHandler, new Camera1Enumerator(captureToTexture));

    this.captureToTexture = captureToTexture;
  }

  @Override
  protected void createCameraSession(CameraSession.CreateSessionCallback createSessionCallback,
      CameraSession.Events events, Context applicationContext,
      SurfaceTextureHelper surfaceTextureHelper, String cameraName, int width, int height,
      int framerate) {
		CameraSession.CreateSessionCallback createSessionCallbackWrapper = new CameraSession.CreateSessionCallback() {
			@Override
			public void onDone(CameraSession session) {
				Camera1Capturer.this.camera1Session = (Camera1Session) session;
				createSessionCallback.onDone(session);
			}

			@Override
			public void onFailure(CameraSession.FailureType failureType, String error) {
				createSessionCallback.onFailure(failureType, error);
			}
		};

    Camera1Session.create(createSessionCallbackWrapper, events, captureToTexture, applicationContext,
        surfaceTextureHelper, Camera1Enumerator.getCameraIndex(cameraName), width, height,
        framerate);
  }

  @Override
	public void setFlashlight(boolean value, FlashlightHandler flashlightEventsHandler) {
    try {
      camera1Session.setFlashlight(value);
      flashlightEventsHandler.onFlashlightSetDone();
    } catch (Throwable throwable) {
      flashlightEventsHandler.onFlashlightSetError(throwable.getMessage());
    }
	}
}
