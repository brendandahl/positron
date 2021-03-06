/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.background.fxa;

import org.mozilla.gecko.background.fxa.FxAccountClient20.AccountStatusResponse;
import org.mozilla.gecko.background.fxa.FxAccountClient20.RequestDelegate;
import org.mozilla.gecko.background.fxa.FxAccountClient20.RecoveryEmailStatusResponse;
import org.mozilla.gecko.background.fxa.FxAccountClient20.TwoKeys;
import org.mozilla.gecko.fxa.FxAccountDevice;
import org.mozilla.gecko.sync.ExtendedJSONObject;

public interface FxAccountClient {
  public void accountStatus(String uid, RequestDelegate<AccountStatusResponse> requestDelegate);
  public void recoveryEmailStatus(byte[] sessionToken, RequestDelegate<RecoveryEmailStatusResponse> requestDelegate);
  public void keys(byte[] keyFetchToken, RequestDelegate<TwoKeys> requestDelegate);
  public void sign(byte[] sessionToken, ExtendedJSONObject publicKey, long certificateDurationInMilliseconds, RequestDelegate<String> requestDelegate);
  public void registerOrUpdateDevice(byte[] sessionToken, FxAccountDevice device, RequestDelegate<FxAccountDevice> requestDelegate);
  public void deviceList(byte[] sessionToken, RequestDelegate<FxAccountDevice[]> requestDelegate);
}
