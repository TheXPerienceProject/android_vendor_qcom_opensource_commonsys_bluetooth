/*
 * Copyright (c) 2017, 2018 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *        * Redistributions of source code must retain the above copyright
 *            notice, this list of conditions and the following disclaimer.
 *        * Redistributions in binary form must reproduce the above copyright
 *            notice, this list of conditions and the following disclaimer in the
 *            documentation and/or other materials provided with the distribution.
 *        * Neither the name of The Linux Foundation nor
 *            the names of its contributors may be used to endorse or promote
 *            products derived from this software without specific prior written
 *            permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.    IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package org.codeaurora.bluetooth.bttestapp;


import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothAvrcpController;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothProfile.ServiceListener;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.media.session.PlaybackState;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;

import android.media.browse.MediaBrowser;
import android.media.browse.MediaBrowser.MediaItem;
import android.media.MediaDescription;
import android.media.session.MediaController;
import android.media.session.MediaSession;
import android.media.session.MediaSession.QueueItem;
import android.widget.TextClock;

import java.util.List;

public class AvrcpTestActivity extends Activity implements OnClickListener {

    private final String TAG = "BtTestAvrcp";
    private Button mBtnPlayPause;
    private Button mBtnDisconnectBip;
    private Button mBtnConnectBip;
    private Button mBtnGetFolderItems;
    private Button mBtnSetBrowsedPlayer;
    private Button mBtnChangePath;
    private final String STATUS_PLAY = "play";
    private final String STATUS_PAUSE = "pause";

    static final int MSG_DISCONNECT_BIP = 600;
    static final int MSG_CONNECT_BIP = 601;
    static final int MSG_SET_BROWSED_PLAYER = 602;
    static final int MSG_GET_FOLDERITMES = 603;
    static final int MSG_CHANGE_PATH = 604;

    private static final String ACTION_TRACK_EVENT =
            "android.bluetooth.avrcp-controller.profile.action.TRACK_EVENT";

    private static final String EXTRA_PLAYBACK =
            "android.bluetooth.avrcp-controller.profile.extra.PLAYBACK";

    /* Object used to connect to MediaBrowseService of BT-AVRCP app */
    private MediaBrowser mMediaBrowser = null;
    private MediaController mMediaController = null;
    private BluetoothAvrcpController mAvrcpController = null;

    private Context mContext;

    /* Browse connection state callback handler */
    private MediaBrowser.ConnectionCallback browseMediaConnectionCallback =
            new MediaBrowser.ConnectionCallback() {
        @Override
        public void onConnected() {
            Log.d(TAG, "mediaBrowser CONNECTED");
            mMediaController = new MediaController(mContext, mMediaBrowser.getSessionToken());
        }

        @Override
        public void onConnectionFailed() {
            Log.e(TAG, "mediaBrowser Connection failed");
        }

        @Override
        public void onConnectionSuspended() {
            Log.e(TAG, "mediaBrowser SUSPENDED");
        }
    };

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.i(TAG, "action " + action);
            if (action.equals(ACTION_TRACK_EVENT)) {
               PlaybackState ps = intent.getParcelableExtra(EXTRA_PLAYBACK);
               if (ps != null && mBtnPlayPause != null
                   && (ps.getState() == PlaybackState.STATE_PAUSED
                   || ps.getState() == PlaybackState.STATE_STOPPED)) {
                   mBtnPlayPause.setText(STATUS_PLAY);
                   Log.i(TAG, " button status play ");
               } else if (ps != null && mBtnPlayPause != null
                   && ps.getState() == PlaybackState.STATE_PLAYING) {
                   mBtnPlayPause.setText(STATUS_PAUSE);
                   Log.i(TAG, " button status pause ");
               }
            }
        }
    };
    private final BluetoothAdapter mAdapter = BluetoothAdapter.getDefaultAdapter();
    TextClock te;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.layout_avrcp);
        Log.i(TAG, "onCreate ");
        mBtnPlayPause = (Button) findViewById(R.id.id_btn_play_pause);
        mBtnPlayPause.setText(STATUS_PLAY);
        mBtnPlayPause.setOnClickListener(this);
        mBtnDisconnectBip = (Button) findViewById(R.id.id_btn_disconnect_bip);
        mBtnDisconnectBip.setOnClickListener(this);
        mBtnConnectBip = (Button) findViewById(R.id.id_btn_connect_bip);
        mBtnConnectBip.setOnClickListener(this);
        mBtnSetBrowsedPlayer = (Button) findViewById(R.id.id_btn_set_browsed_player);
        mBtnSetBrowsedPlayer.setOnClickListener(this);
        mBtnChangePath= (Button) findViewById(R.id.id_btn_change_path);
        mBtnChangePath.setOnClickListener(this);
        mBtnGetFolderItems= (Button) findViewById(R.id.id_btn_get_folder_items);
        mBtnGetFolderItems.setOnClickListener(this);
        mContext = getApplicationContext();
        mAdapter.getProfileProxy(getApplicationContext(), mAvrcpControllerServiceListener,
                BluetoothProfile.AVRCP_CONTROLLER);
        te=(TextClock)findViewById(R.id.textClock);
        mMediaBrowser = new MediaBrowser(mContext,
                new ComponentName("com.android.bluetooth",
                       "com.android.bluetooth.avrcpcontroller.BluetoothMediaBrowserService"),
                       browseMediaConnectionCallback, null);
        mMediaBrowser.connect();
        // receive playback state change
        IntentFilter filter = new IntentFilter();
        filter.addAction(ACTION_TRACK_EVENT);
        registerReceiver(mReceiver, filter);
    }

    private final ServiceListener mAvrcpControllerServiceListener = new ServiceListener() {
        @Override
        public void onServiceConnected(int profile, BluetoothProfile proxy) {
            Log.i(TAG," onServiceConnected");
            if (profile == BluetoothProfile.AVRCP_CONTROLLER) {
                mAvrcpController = (BluetoothAvrcpController) proxy;
            }
        }

        @Override
        public void onServiceDisconnected(int profile) {
            Log.i(TAG," onServiceDisconnected");
            if (profile == BluetoothProfile.AVRCP_CONTROLLER) {
                mAvrcpController = null;
            }
        }
    };

    @Override
    protected void onDestroy() {
        Log.i(TAG, "onDestroy ");
        super.onDestroy();
        unregisterReceiver(mReceiver);
    }

    @Override
    public void onClick(View v) {
        if (v == mBtnPlayPause ) {
        Log.i(TAG," time :"+te.getText());
        mBtnPlayPause.setEnabled(false);
        sendCommand();
        mBtnPlayPause.setEnabled(true);
        } else if (v == mBtnDisconnectBip ) {
            List<BluetoothDevice> devs = mAvrcpController.getConnectedDevices();
            Log.d(TAG, "disconnectBip");
            for (BluetoothDevice device : devs) {
                mAvrcpController.sendGroupNavigationCmd(device, MSG_DISCONNECT_BIP, 0 );
            }
        } else if (v == mBtnConnectBip ) {
            List<BluetoothDevice> devs = mAvrcpController.getConnectedDevices();
            Log.d(TAG, "connectBip");
            for (BluetoothDevice device : devs) {
                Log.d(TAG, "send connectBip");
                mAvrcpController.sendGroupNavigationCmd(device, MSG_CONNECT_BIP, 0);
            }
        } else if (v == mBtnSetBrowsedPlayer ) {
            List<BluetoothDevice> devs = mAvrcpController.getConnectedDevices();
            Log.d(TAG, "setBrowsedPlayer");
            for (BluetoothDevice device : devs) {
                Log.d(TAG, "send setBrowsedPlayer");
                mAvrcpController.sendGroupNavigationCmd(device, MSG_SET_BROWSED_PLAYER, 0);
            }
        } else if (v == mBtnGetFolderItems) {
            List<BluetoothDevice> devs = mAvrcpController.getConnectedDevices();
            Log.d(TAG, "getFolderItmes");
            for (BluetoothDevice device : devs) {
                Log.d(TAG, "send getFolderItmes");
                mAvrcpController.sendGroupNavigationCmd(device, MSG_GET_FOLDERITMES, 0);
            }
        } else if (v == mBtnChangePath) {
            List<BluetoothDevice> devs = mAvrcpController.getConnectedDevices();
            Log.d(TAG, "changePath");
            for (BluetoothDevice device : devs) {
                Log.d(TAG, "send changePath");
                mAvrcpController.sendGroupNavigationCmd(device, MSG_CHANGE_PATH, 0);
            }
        }
    }

    private void sendCommand() {
        if (mMediaController == null || mAvrcpController ==null) {
            Log.i(TAG, "mMediaController :" + mMediaController
                    +" mAvrcpController :" + mAvrcpController);
            return;
        }
        String status = mBtnPlayPause.getText().toString().trim();
        try {
            if (status.equals(STATUS_PLAY)) {
                sendPlay();
            } else if (status.equals(STATUS_PAUSE)) {
                sendPause();
            }
        } catch (Exception e) {
            Log.e(TAG, e.toString());
            e.printStackTrace();
        }
    }

    private void sendPlay() {
        Log.d(TAG, "sendPlay");
        if (mMediaController != null) {
            Log.d(TAG, "calling play()");
            mMediaController.getTransportControls().play();
            Log.d(TAG, "sendPlay complete ");
        }
    }

    private void sendPause() {
        Log.d(TAG, "sendPause");
        if (mMediaController != null) {
            Log.d(TAG, "calling pause()");
            mMediaController.getTransportControls().pause();
            Log.d(TAG, "sendpause complete ");
        }
    }
}
