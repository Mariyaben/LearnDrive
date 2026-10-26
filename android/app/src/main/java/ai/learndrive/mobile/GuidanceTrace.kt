package ai.learndrive.mobile

import org.json.JSONArray
import org.json.JSONObject

/** Versioned local diagnostic format shared with tools/review_video.py. */
object GuidanceTrace {
    /** Provider content stays transient. Only offline route context is exportable. */
    fun navigationForExport(context:DoubleArray?,google:Boolean):DoubleArray?=if(google)null else context
    fun decision(d:GuidanceDecision,time:Double,source:String,speedSource:String,frame:FrameContext?,reason:String,
        emergencies:Boolean,spatialReadiness:Int?=null,setupState:Int?=null,levelReferenceId:Long?=null,
        calibrationId:Long?=null,advice:Telemetry?=null,measured:Telemetry?=null,navigation:DoubleArray?=null,
        coachingDiagnostics:String?=null,capability:VisionPerformanceSnapshot?=null):String = JSONObject().apply {
        put("schema_version",1);put("event","decision");put("time",time)
        put("source",source);put("source_id",d.sourceId);put("sequence",d.sequence)
        put("settings_revision",d.settingsRevision)
        put("prompt",d.prompt);put("origin",d.origin)
        put("candidates",JSONArray(listOf(d.tactical,d.reflex,d.terrain,d.camera)))
        put("suppress",d.suppress);put("eligible",d.eligible);put("blocked",d.blocked)
        put("capture_time",d.captureTime.takeIf {it.isFinite()&&it>=0}?:JSONObject.NULL)
        put("scene_age",d.sceneAge.takeIf {d.captureTime>=0&&it.isFinite()}?:JSONObject.NULL)
        put("semantic_available",d.semanticAvailable);put("spatial_available",d.spatialAvailable)
        put("reason",reason);put("speed_source",speedSource);put("research_emergencies",emergencies)
        put("calibration_origin",frame?.calibrationOrigin?:"unknown")
        put("recorded_time",frame?.recordedTime?:JSONObject.NULL)
        put("spatial_readiness",spatialReadiness?:JSONObject.NULL)
        put("setup_state",setupState?:JSONObject.NULL)
        put("camera_actor",if(d.actorId>0)JSONObject().apply {
            put("id",d.actorId);put("category",d.actorCategory);put("evidence_kind",d.evidenceKind)
            put("box",JSONArray(d.actorBox));put("identity_scope","source-local camera attention track")
        } else JSONObject.NULL)
        put("level_reference_id",levelReferenceId?:JSONObject.NULL)
        put("calibration_id",calibrationId?:JSONObject.NULL)
        put("advice_telemetry",advice?.let {telemetry(it)}?:JSONObject.NULL)
        put("measured_phone_telemetry",measured?.let {telemetry(it)}?:JSONObject.NULL)
        put("navigation",navigation?.let {navigationFields(it)}?:JSONObject.NULL)
        put("coaching",coachingDiagnostics?.let {JSONObject(it)}?:JSONObject.NULL)
        put("capability",capability?.let {JSONObject().apply {
            put("profile",it.capabilityProfile);put("reason",it.capabilityReason)
            put("advanced_available",it.advancedAvailable);put("spatial_samples",it.capabilitySamples)
            put("slow_spatial_samples",it.capabilitySlowSamples);put("spatial_failures",it.capabilityFailures)
            put("last_detector_ms",number(it.lastDetectorMs));put("last_spatial_ms",number(it.lastSpatialMs))
            put("last_frame_ms",number(it.lastFrameMs))
        }}?:JSONObject.NULL)
    }.toString()
    fun accepted(d:GuidanceDecision,time:Double):String=JSONObject().apply {
        put("schema_version",1);put("event","accepted");put("time",time)
        put("source_id",d.sourceId);put("sequence",d.sequence);put("prompt",d.prompt)
        put("settings_revision",d.settingsRevision);put("voice_submitted",true)
        put("audible_playback_confirmed",false)
    }.toString()
    fun coachingAccepted(d:GuidanceDecision,time:Double,voiceSubmitted:Boolean):String=JSONObject().apply {
        put("schema_version",1);put("event","coaching_accepted");put("time",time)
        put("source_id",d.sourceId);put("sequence",d.sequence);put("prompt",d.prompt)
        put("settings_revision",d.settingsRevision);put("voice_submitted",voiceSubmitted)
        put("audible_playback_confirmed",false)
    }.toString()
    private fun number(value:Double?)=value?.takeIf {it.isFinite()}?:JSONObject.NULL
    private fun navigationFields(v:DoubleArray)=JSONObject().apply {
        put("route_id",v[0].toLong());put("feature_id",v[1].toLong());put("feature",v[2].toInt());put("match",v[3].toInt())
        put("fix_time",number(v[4]));put("progress_m",number(v[5]));put("distance_m",number(v[6]))
        put("heading_change_degrees",number(v[7]));put("estimated_radius_m",number(v[8]))
        // Google Navigation road names/speed limits must not survive the user
        // session. This persisted format has no provider discriminator, so
        // redact provider speed-limit content for every navigation source.
        // Live policy still receives the original in-memory navigation array.
        put("speed_limit_kmh",JSONObject.NULL);put("speed_limit_available",v[9].isFinite()&&v[9]>0)
        put("speed_limit_redaction","navigation provider content is not persisted")
        put("valid",v[10]!=0.0)
        put("maneuver_type",v[11].toInt());put("maneuver_shape_index",v[12].toInt());put("maneuver_distance_m",number(v[13]))
        put("gps_speed_kmh",number(v[14]));put("gps_speed_accuracy_mps",number(v[15]));put("horizontal_accuracy_m",number(v[16]))
    }
    fun navigation(v:DoubleArray,time:Double)=JSONObject().apply {
        put("schema_version",1);put("event","navigation_context");put("time",time);put("navigation",navigationFields(v))
    }.toString()
    private fun telemetry(t:Telemetry)=JSONObject().apply {
        put("time",number(t.time));put("speed_kmh",if(t.speedValid)number(t.speed) else JSONObject.NULL)
        put("pitch_degrees",if(t.pitchValid)number(t.pitch) else JSONObject.NULL)
        put("longitudinal_acceleration",number(t.acceleration));put("calibrated",t.calibrated)
        put("speed_valid",t.speedValid);put("pitch_valid",t.pitchValid);put("braking",t.braking)
        put("hard_braking",t.hardBraking);put("shock",t.shock)
    }
    fun source(id:Long,kind:CaptureSource,time:Double)=JSONObject().apply {
        put("schema_version",1);put("event","source_start");put("time",time);put("source_id",id);put("source",kind.name)
    }.toString()
    fun frame(v:VisionResult)=JSONObject().apply {
        val f=v.frame
        put("schema_version",1);put("event","frame_observation");put("source_id",f?.sourceId?:0)
        put("source",f?.source?.name?:"unknown");put("capture_time",number(v.time));put("completed_time",number(v.completedAt))
        put("frame_sequence",f?.frameSequence?:0)
        put("semantic_capture_time",number(v.semanticCaptureTime));put("recorded_time",number(f?.recordedTime))
        put("semantic_completed_time",number(v.semanticCompletedAt));put("semantic_processing_ms",number(v.semanticProcessingMs))
        put("image_width",f?.imageWidth?:0);put("image_height",f?.imageHeight?:0);put("rotation_degrees",f?.rotationDegrees?:0)
        put("crop",JSONArray(listOf(f?.cropLeft?:0,f?.cropTop?:0,f?.cropWidth?:0,f?.cropHeight?:0)))
        put("pixel_range",f?.pixelRange?:"unknown");put("calibration_origin",f?.calibrationOrigin?:"unknown")
        put("pixel_range_origin",f?.pixelRangeOrigin?:"unknown");put("pixel_format",f?.pixelFormat?:0)
        put("sensor_intrinsics",f?.intrinsics?.let {JSONArray(it.toList())}?:JSONObject.NULL)
        put("original_recorded_motion_available",if(f?.source==CaptureSource.RECORDING)false else JSONObject.NULL);put("spatial_readiness",v.spatialReadiness)
        put("processing_ms",number(v.processingMs));put("tti_seconds",number(v.tti));put("approach_confidence",number(v.confidence))
        put("in_path",v.inPath);put("road_valid",v.road?.valid?:false);put("road_bend",v.road?.bend?:0)
        put("detections",JSONArray(v.detections.take(64).map {d->JSONObject().apply {
            put("x",d.x.toDouble());put("y",d.y.toDouble());put("width",d.width.toDouble());put("height",d.height.toDouble())
            put("score",d.score.toDouble());put("category",d.category)
        }}))
    }.toString()
}
