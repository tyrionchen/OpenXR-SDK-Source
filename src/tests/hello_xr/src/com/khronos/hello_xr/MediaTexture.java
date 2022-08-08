package com.khronos.hello_xr;

import android.content.Context;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.util.Log;
import android.view.Surface;

import java.io.IOException;

public class MediaTexture implements MediaPlayer.OnVideoSizeChangedListener, SurfaceTexture.OnFrameAvailableListener {
    private static final String TAG = "MediaTexture";
    private boolean mUpdateSurface;

    private final SurfaceTexture mSurfaceTexture;
    private final float[] mSTMatrix = new float[16];
    
    // 读取视频流转换为图像纹理
    public MediaTexture(int textureId, Context context) {
        Log.i(TAG, "MediaTexture() textureId:" + textureId);
        MediaPlayer mMediaPlayer = new MediaPlayer();
        try {
            mMediaPlayer.setDataSource(context.getAssets().openFd("demo_video.mp4"));
        } catch (IOException e) {
            Log.e(TAG, "setDataSource failed:" + e.getMessage());
        }
        mMediaPlayer.setAudioStreamType(AudioManager.STREAM_MUSIC);
        mMediaPlayer.setLooping(true);
        mMediaPlayer.setOnVideoSizeChangedListener(this);

        mSurfaceTexture = new SurfaceTexture(textureId);
        mSurfaceTexture.setOnFrameAvailableListener(this);

        Surface surface = new Surface(mSurfaceTexture);
        mMediaPlayer.setSurface(surface);
        surface.release();

        try {
            mMediaPlayer.prepare();
        } catch (Exception t) {
            Log.e(TAG, "media player prepare failed");
        }
        mMediaPlayer.start();
    }

    @Override
    public void onVideoSizeChanged(MediaPlayer mediaPlayer, int i, int i1) {
        Log.i(TAG, String.format("onVideoSizeChanged[%d,%d]", i, i1));
    }

    @Override
    public void onFrameAvailable(SurfaceTexture surfaceTexture) {
        Log.i(TAG, "onFrameAvailable");
        synchronized (this) {
            mUpdateSurface = true;
        }
    }
    
    public void updateTexture(float[] mtx) {
        synchronized (this){
            if (mUpdateSurface){
                mSurfaceTexture.updateTexImage();
                mSurfaceTexture.getTransformMatrix(mSTMatrix);
                System.arraycopy(mSTMatrix, 0, mtx, 0, 16);
                mUpdateSurface = false;
            }
        }
    }
}

