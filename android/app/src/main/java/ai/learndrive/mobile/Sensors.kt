package ai.learndrive.mobile

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.HandlerThread
import android.os.SystemClock
import androidx.core.content.ContextCompat

fun now() = SystemClock.elapsedRealtimeNanos() / 1e9

class Sensors(private val context: Context, private val core: NativeCore) : SensorEventListener, LocationListener, AutoCloseable {
    private val manager = context.getSystemService(SensorManager::class.java)
    private val locations = context.getSystemService(LocationManager::class.java)
    private var thread: HandlerThread? = null
    private var gpsRequested=false
    private val gyro = DoubleArray(3)
    private val linear = DoubleArray(3)
    private val gravity = DoubleArray(3)
    private var gyroTime = -1.0
    private var linearTime = -1.0
    private var gravityTime = -1.0
    @Volatile var latestPosition:Location?=null
        private set
    @Volatile var motionAvailable=false
        private set
    @Volatile var motionStatus="Waiting for phone motion sensors"
        private set
    fun start() {
        if(thread==null) {
            val worker=HandlerThread("imu-100hz").also { it.start() };thread=worker
            val handler=android.os.Handler(worker.looper)
            var accelerometerReady=false;var gyroReady=false
            for(type in listOf(Sensor.TYPE_ACCELEROMETER,Sensor.TYPE_GYROSCOPE,Sensor.TYPE_LINEAR_ACCELERATION,Sensor.TYPE_GRAVITY)) {
                val registered=manager.getDefaultSensor(type)?.let {manager.registerListener(this,it,10_000,handler)}?:false
                if(type==Sensor.TYPE_ACCELEROMETER)accelerometerReady=registered
                if(type==Sensor.TYPE_GYROSCOPE)gyroReady=registered
            }
            motionAvailable=accelerometerReady&&gyroReady
            motionStatus=if(motionAvailable)"Phone motion sensors ready" else "Phone slope unavailable: accelerometer or gyroscope missing"
        }
        val worker=thread?:return
        // Permission can arrive after IMU startup. Subscribe then as well.
        if(!gpsRequested&&ContextCompat.checkSelfPermission(context,Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED && locations.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            locations.requestLocationUpdates(LocationManager.GPS_PROVIDER,1000L,0f,this,worker.looper)
            gpsRequested=true
        }
    }
    override fun onSensorChanged(event: SensorEvent) {
        val t=event.timestamp/1e9
        when(event.sensor.type) {
            Sensor.TYPE_GYROSCOPE -> {for(i in 0..2)gyro[i]=event.values[i].toDouble();gyroTime=t}
            Sensor.TYPE_LINEAR_ACCELERATION -> {for(i in 0..2)linear[i]=event.values[i].toDouble();linearTime=t}
            Sensor.TYPE_GRAVITY -> {for(i in 0..2)gravity[i]=event.values[i].toDouble();gravityTime=t}
            Sensor.TYPE_ACCELEROMETER -> if(t-gyroTime in 0.0..0.05) {
                val a=DoubleArray(3) { event.values[it].toDouble() }
                val hasLinear=t-linearTime in 0.0..0.05
                val hasGravity=t-gravityTime in 0.0..0.05
                val motion=if(hasLinear)linear else if(hasGravity)DoubleArray(3){a[it]-gravity[it]} else linear
                core.imu(t,a,gyro,motion,hasLinear||hasGravity)
            }
        }
    }
    override fun onLocationChanged(location: Location) {
        if(location.isFromMockProvider)return // Test positions cannot qualify ordinary road speed or route evidence.
        latestPosition=Location(location)
        core.navigationFix(location.elapsedRealtimeNanos/1e9,location.latitude,location.longitude,
            if(location.hasAccuracy())location.accuracy.toDouble() else Double.POSITIVE_INFINITY,
            location.speed.toDouble()*3.6,if(location.hasSpeedAccuracy())location.speedAccuracyMetersPerSecond.toDouble() else Double.POSITIVE_INFINITY,
            location.hasSpeed()&&location.hasSpeedAccuracy(),location.bearing.toDouble(),location.hasBearing()&&location.hasBearingAccuracy()&&location.bearingAccuracyDegrees<=30)
        if(location.hasSpeed()&&location.hasSpeedAccuracy())
            core.gps(location.elapsedRealtimeNanos/1e9,location.speed.toDouble(),location.speedAccuracyMetersPerSecond.toDouble().coerceAtLeast(.5))
    }
    override fun onAccuracyChanged(sensor:Sensor?,accuracy:Int) = Unit
    override fun close() {manager.unregisterListener(this);locations.removeUpdates(this);gpsRequested=false;thread?.quitSafely();thread=null}
}
