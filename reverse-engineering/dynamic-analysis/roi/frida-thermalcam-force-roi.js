Java.perform(function () {
  var HomeActivity = Java.use("com.hti.xtherm.hti160203u.ui.activity.HomeActivity");
  var F2Helper = Java.use("com.hti.xtherm.hti160203u.helper.F2Helper");
  var JavaInterface = Java.use("com.hcusbsdk.Interface.JavaInterface");
  var ThermometryBasicParam = Java.use("com.hcusbsdk.Interface.USB_THERMOMETRY_BASIC_PARAM");
  var UsbRoiSearch = Java.use("com.hcusbsdk.Interface.USB_ROI_MAX_TEMPERATURE_SEARCH");
  var UsbRoiResult = Java.use("com.hcusbsdk.Interface.USB_ROI_MAX_TEMPERATURE_SEARCH_RESULT");
  var Calendar = Java.use("java.util.Calendar");

  function nowIso() {
    return new Date().toISOString();
  }

  function emit(kind, payload) {
    console.log(JSON.stringify({
      ts: nowIso(),
      kind: kind,
      payload: payload
    }));
  }

  function safeString(value) {
    try {
      if (value === null || value === undefined) {
        return null;
      }
      return String(value);
    } catch (e) {
      return "<toString failed: " + e + ">";
    }
  }

  function getField(instance, fieldName) {
    var field = instance.getClass().getDeclaredField(fieldName);
    field.setAccessible(true);
    return field.get(instance);
  }

  function getIntField(instance, fieldName) {
    var field = instance.getClass().getDeclaredField(fieldName);
    field.setAccessible(true);
    return field.getInt(instance);
  }

  function serializeRegion(region) {
    return {
      id: region.byROIRegionID.value,
      enabled: region.byROIRegionEnabled.value,
      x: region.dwROIRegionX.value,
      y: region.dwROIRegionY.value,
      width: region.dwROIRegionWidth.value,
      height: region.dwROIRegionHeight.value,
      distance: region.dwDistance.value
    };
  }

  function serializeRegionInfo(info) {
    return {
      id: info.byROIRegionID.value,
      max_temp_raw: info.dwMaxROIRegionTemperature.value,
      max_temp_c_frame_formula: (info.dwMaxROIRegionTemperature.value / 64.0) - 50.0,
      visible_x: info.dwVisibleROIRegionMaxTemperaturePointX.value,
      visible_y: info.dwVisibleROIRegionMaxTemperaturePointY.value,
      thermal_x: info.dwThermalROIRegionMaxTemperaturePointX.value,
      thermal_y: info.dwThermalROIRegionMaxTemperaturePointY.value
    };
  }

  function serializeResult(result) {
    var out = {
      byROIRegionNum: result.byROIRegionNum.value,
      dwJpegPicLen: result.dwJpegPicLen.value,
      dwMaxP2PTemperature: result.dwMaxP2PTemperature.value,
      dwMaxP2PTemperature_c_frame_formula: (result.dwMaxP2PTemperature.value / 64.0) - 50.0,
      visibleP2P: {
        x: result.dwVisibleP2PMaxTemperaturePointX.value,
        y: result.dwVisibleP2PMaxTemperaturePointY.value
      },
      thermalP2P: {
        x: result.dwThermalP2PMaxTemperaturePointX.value,
        y: result.dwThermalP2PMaxTemperaturePointY.value
      },
      regions: []
    };
    var count = result.byROIRegionNum.value;
    if (count < 0) {
      count += 256;
    }
    for (var i = 0; i < Math.min(count, 10); i++) {
      out.regions.push(serializeRegionInfo(result.struThermalROIRegionInfo.value[i]));
    }
    return out;
  }

  function fillTimestamp(req) {
    var cal = Calendar.getInstance();
    req.wMillisecond.value = cal.get(14);
    req.bySecond.value = cal.get(13);
    req.byMinute.value = cal.get(12);
    req.byHour.value = cal.get(11);
    req.byDay.value = cal.get(5);
    req.byMonth.value = cal.get(2) + 1;
    req.wYear.value = cal.get(1);
  }

  function makeRequest(spec, distance) {
    var req = UsbRoiSearch.$new();
    fillTimestamp(req);
    req.byJpegPicEnabled.value = spec.jpeg_enabled ? 1 : 0;
    req.byMaxTemperatureOverlay.value = spec.max_overlay ? 1 : 0;
    req.byRegionsOverlay.value = spec.region_overlay ? 1 : 0;
    req.byROIRegionNum.value = 1;

    var region = req.struThermalROIRegion.value[0];
    region.byROIRegionID.value = 1;
    region.byROIRegionEnabled.value = 1;
    region.dwROIRegionX.value = spec.x;
    region.dwROIRegionY.value = spec.y;
    region.dwROIRegionWidth.value = spec.width;
    region.dwROIRegionHeight.value = spec.height;
    region.dwDistance.value = distance;
    return req;
  }

  function attempt(helper, loginUserId, spec) {
    var basic = getField(helper, "mThermometryBasicParam");
    if (basic) {
      basic = Java.cast(basic, ThermometryBasicParam);
    }
    var distance = basic ? basic.dwDistance.value : 0;
    var javaInterface = getField(helper, "javaInterface");
    if (!javaInterface) {
      javaInterface = JavaInterface.getInstance();
    } else {
      javaInterface = Java.cast(javaInterface, JavaInterface);
    }

    var req = makeRequest(spec, distance);
    var result = UsbRoiResult.$new();
    emit("roi_attempt_enter", {
      label: spec.label,
      login_userid: loginUserId,
      is_preview: helper.isPreview(),
      thermometry_distance: distance,
      thermometry_channel: basic ? basic.byChannelID.value : null,
      request: {
        jpeg_enabled: req.byJpegPicEnabled.value,
        max_overlay: req.byMaxTemperatureOverlay.value,
        region_overlay: req.byRegionsOverlay.value,
        region_num: req.byROIRegionNum.value,
        region0: serializeRegion(req.struThermalROIRegion.value[0])
      }
    });

    var ok = javaInterface.USB_GetROITemperatureSearch(loginUserId, req, result);
    emit("roi_attempt_exit", {
      label: spec.label,
      call_kind: "public_jni",
      ok: !!ok,
      result: serializeResult(result)
    });
  }

  function attemptPrivateJna(helper, loginUserId, spec) {
    var basic = getField(helper, "mThermometryBasicParam");
    if (basic) {
      basic = Java.cast(basic, ThermometryBasicParam);
    }
    var distance = basic ? basic.dwDistance.value : 0;
    var javaInterface = getField(helper, "javaInterface");
    if (!javaInterface) {
      javaInterface = JavaInterface.getInstance();
    } else {
      javaInterface = Java.cast(javaInterface, JavaInterface);
    }

    var req = makeRequest(spec, distance);
    var result = UsbRoiResult.$new();
    emit("roi_attempt_enter", {
      label: spec.label,
      call_kind: "private_jna_reflect",
      login_userid: loginUserId,
      is_preview: helper.isPreview(),
      thermometry_distance: distance,
      thermometry_channel: basic ? basic.byChannelID.value : null,
      request: {
        jpeg_enabled: req.byJpegPicEnabled.value,
        max_overlay: req.byMaxTemperatureOverlay.value,
        region_overlay: req.byRegionsOverlay.value,
        region_num: req.byROIRegionNum.value,
        region0: serializeRegion(req.struThermalROIRegion.value[0])
      }
    });

    var cls = javaInterface.getClass();
    var methods = cls.getDeclaredMethods();
    var target = null;
    for (var i = 0; i < methods.length; i++) {
      var method = methods[i];
      if (String(method.getName()) === "USB_GetROITemperatureSearch_jna") {
        target = method;
        break;
      }
    }
    if (target === null) {
      emit("roi_probe_error", { error: "private USB_GetROITemperatureSearch_jna not found" });
      return;
    }
    target.setAccessible(true);
    var Integer = Java.use("java.lang.Integer");
    var boxedLogin = Integer.valueOf(loginUserId);
    var args = Java.array("java.lang.Object", [boxedLogin, req, result]);
    var rv = target.invoke(javaInterface, args);
    emit("roi_attempt_exit", {
      label: spec.label,
      call_kind: "private_jna_reflect",
      ok: safeString(rv) === "true",
      retval_raw: safeString(rv),
      result: serializeResult(result)
    });
  }

  function runProbe() {
    var found = false;
    Java.choose("com.hti.xtherm.hti160203u.ui.activity.HomeActivity", {
      onMatch: function (activity) {
        if (found) {
          return;
        }
        found = true;
        try {
          var helper = Java.cast(getField(activity, "mF2Helper"), F2Helper);
          var loginUserId = getIntField(helper, "login_userid");
          emit("roi_context", {
            activity: safeString(activity),
            helper: safeString(helper),
            login_userid: loginUserId,
            is_preview: helper.isPreview()
          });

          var specs = [
            { label: "thermal_full_no_overlay", x: 0, y: 0, width: 256, height: 192, jpeg_enabled: false, max_overlay: false, region_overlay: false },
            { label: "thermal_full_with_overlay", x: 0, y: 0, width: 256, height: 192, jpeg_enabled: false, max_overlay: true, region_overlay: true },
            { label: "center_box_64x64", x: 96, y: 64, width: 64, height: 64, jpeg_enabled: false, max_overlay: false, region_overlay: false }
          ];

          for (var i = 0; i < specs.length; i++) {
            attempt(helper, loginUserId, specs[i]);
          }
          attemptPrivateJna(helper, loginUserId, specs[0]);
        } catch (e) {
          emit("roi_probe_error", { error: safeString(e), stack: e && e.stack ? String(e.stack) : null });
        }
      },
      onComplete: function () {
        if (!found) {
          emit("roi_probe_error", { error: "HomeActivity not found" });
        }
        setTimeout(function () {
          emit("roi_probe_done", {});
        }, 250);
      }
    });
  }

  setTimeout(runProbe, 1000);
});
