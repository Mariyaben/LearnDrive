package ai.learndrive.mobile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Each learner keeps their own record and settings; the phone's first learner keeps the old storage. */
class LearnersTest {
    private class Memory:Learners.KeyValue {
        val map=HashMap<String,String>()
        override fun get(key:String)=map[key]
        override fun put(key:String,value:String){map[key]=value}
        override fun remove(key:String){map.remove(key)}
    }
    private fun ids():()->String {var n=0;return {"learner%06d".format(++n)}}

    @Test fun anUpdatedPhoneHasOneLearnerWhoKeepsTheOldStorage() {
        val memory=Memory();val learners=Learners(memory,ids())
        assertEquals(1,learners.count)
        assertEquals(Learners.FIRST_NAME,learners.current.name)
        // 0.9.7's settings, practice records and "You first" record: nothing is moved.
        assertEquals(Learners.Storage("driving_advice","learning_records","learner-record.json",true),learners.storage(learners.current.id))
        // The same learner on the next start.
        assertEquals(learners.current,Learners(memory,ids()).current)
    }

    @Test fun anAddedLearnerHasStorageOfTheirOwn() {
        val memory=Memory();val learners=Learners(memory,ids())
        val asha=learners.add("Asha")!!
        assertEquals(Learners.FIRST_NAME,learners.current.name)   // adding does not switch
        assertTrue(learners.select(asha.id))
        assertEquals(asha,learners.current)
        val own=learners.storage(asha.id)
        assertFalse(own.legacy)
        assertEquals("learner_${asha.id}",own.settings)
        assertEquals("learners/${asha.id}/learner-record.json",own.recordPath)
        val again=Learners(memory,ids())
        assertEquals(asha,again.current)
        assertEquals(listOf(Learners.FIRST_NAME,"Asha"),again.all.map {it.name})
    }

    @Test fun namesAreTidiedAndNeverShared() {
        val learners=Learners(Memory(),ids())
        assertEquals("Asha K",learners.add("  Asha   K ")!!.name)
        assertNull(learners.add("asha k"))
        assertNull(learners.add("   "))
        assertNull(learners.add("x".repeat(Learners.MAX_NAME+1)))
        val ravi=learners.add("Ravi")!!
        assertFalse(learners.rename(ravi.id,"ASHA K"))
        assertTrue(learners.rename(ravi.id,"ravi"))       // a learner may change their own name's case
        assertEquals("ravi",learners.all.last().name)
    }

    @Test fun theLastLearnerStaysAndRemovingTheOneDrivingSwitches() {
        val learners=Learners(Memory(),ids());val first=learners.current
        assertNull(learners.remove(first.id))
        val asha=learners.add("Asha")!!;learners.select(asha.id)
        val removed=learners.remove(asha.id)!!
        assertEquals("learner_${asha.id}",removed.settings)
        assertEquals(first,learners.current)
        // The first learner can go once someone else is on the list; their old storage is handed back.
        val ravi=learners.add("Ravi")!!
        val legacy=learners.remove(first.id)!!
        assertTrue(legacy.legacy)
        assertEquals(ravi,learners.current)
        assertFalse(learners.storage(ravi.id).legacy)
    }

    @Test fun theListHasALimitAndIdsAreNeverReused() {
        val learners=Learners(Memory(),ids())
        repeat(Learners.MAX_LEARNERS-1) {assertNotNull(learners.add("Learner ${it+2}"))}
        assertNull(learners.add("One too many"))
        val same=Learners(Memory()) {"learner000001"}
        assertNull(same.add("Asha"))                      // the generator gave an id already in use
    }

    @Test fun aDamagedListRecovers() {
        val memory=Memory()
        memory.put("ids","bad id!,learner000009,learner000009,learner000010")
        memory.put("current","gone000001")
        val learners=Learners(memory,ids())
        assertEquals(listOf("learner000009","learner000010"),learners.all.map {it.id})
        assertEquals("learner000009",learners.current.id)
        assertEquals(listOf("Learner 1","Learner 2"),learners.all.map {it.name})
        assertNotEquals(true,learners.storage("learner000009").legacy)
    }
}
