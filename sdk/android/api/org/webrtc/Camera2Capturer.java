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

import android.annotation.TargetApi;
import android.content.Context;
import android.hardware.camera2.CameraManager;
import android.os.Build;
import android.support.annotation.Nullable;

@TargetApi(21)
public class Camera2Capturer extends CameraCapturer {
  private final Context context;
  @Nullable private final CameraManager cameraManager;
  private Camera2Session camera2Session;

  public Camera2Capturer(Context context, String cameraName, CameraEventsHandler eventsHandler) {
    super(cameraName, eventsHandler, new Camera2Enumerator(context));

    this.context = context;
    cameraManager = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
  }

  @Override
  protected void createCameraSession(CameraSession.CreateSessionCallback createSessionCallback,
      CameraSession.Events events, Context applicationContext,
      SurfaceTextureHelper surfaceTextureHelper, String cameraName, int width, int height,
      int framerate) {
    CameraSession.CreateSessionCallback createSessionCallbackWrapper = new CameraSession.CreateSessionCallback() {
      @Override
      public void onDone(CameraSession session) {
        Camera2Capturer.this.camera2Session = (Camera2Session) session;
        createSessionCallback.onDone(session);
      }

      @Override
      public void onFailure(CameraSession.FailureType failureType, String error) {
        createSessionCallback.onFailure(failureType, error);
      }
    };
    Camera2Session.create(createSessionCallbackWrapper, events, applicationContext, cameraManager,
        surfaceTextureHelper, cameraName, width, height, framerate);
  }

  @Override
  public void setFlashlight(boolean value, FlashlightHandler flashlightEventsHandler) {
		try {
      camera2Session.setFlashlight(value);
		} catch (Throwable throwable) {
			flashlightEventsHandler.onFlashlightSetError(throwable.getMessage());
		}
  }

}
