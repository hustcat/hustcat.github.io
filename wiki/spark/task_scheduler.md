一个Spark应用程序包括Job、Stage以及Task三个概念：

(1) Job是以Action方法为界，遇到一个Action方法则触发一个Job；
(2) Stage是Job的子集，以RDD宽依赖(即Shuffle)为界，遇到Shuffle做一次划分；
(3) Task是Stage的子集，以并行度(分区数)来衡量，分区数是多少，则有多少个task。

Spark的任务调度总体来说分两级进行，一是Stage级的调度，一是Task级的调度。


* TaskScheduler启动

```
private[spark] class TaskSchedulerImpl private[scheduler](
    val sc: SparkContext,
    val maxTaskFailures: Int,
    private[scheduler] val blacklistTrackerOpt: Option[BlacklistTracker],
    isLocal: Boolean = false)
  extends TaskScheduler with Logging
{
///...
  override def start() {
    backend.start() ///启动SchedulerBackend

    if (!isLocal && conf.getBoolean("spark.speculation", false)) {
      logInfo("Starting speculative execution thread")
      speculationScheduler.scheduleAtFixedRate(new Runnable {
        override def run(): Unit = Utils.tryOrStopSparkContext(sc) {
          checkSpeculatableTasks()
        }
      }, SPECULATION_INTERVAL_MS, SPECULATION_INTERVAL_MS, TimeUnit.MILLISECONDS)
    }
  }
```


*  SchedulerBackend的启动

```
private[spark] class KubernetesClusterSchedulerBackend(
    scheduler: TaskSchedulerImpl,
    val sc: SparkContext)
  extends CoarseGrainedSchedulerBackend(scheduler, sc.env.rpcEnv) {

///...
  override def start(): Unit = {
    super.start()
    if (!Utils.isDynamicAllocationEnabled(sc.conf)) {
      doRequestTotalExecutors(initialExecutors) ///创建executors
    }
  }
```

* 提交任务

```
  override def submitTasks(taskSet: TaskSet) {
    val tasks = taskSet.tasks
    logInfo("Adding task set " + taskSet.id + " with " + tasks.length + " tasks")
    this.synchronized {
      val manager = createTaskSetManager(taskSet, maxTaskFailures)
      val stage = taskSet.stageId
      val stageTaskSets =
        taskSetsByStageIdAndAttempt.getOrElseUpdate(stage, new HashMap[Int, TaskSetManager])
      stageTaskSets(taskSet.stageAttemptId) = manager
      val conflictingTaskSet = stageTaskSets.exists { case (_, ts) =>
        ts.taskSet != taskSet && !ts.isZombie
      }
      if (conflictingTaskSet) {
        throw new IllegalStateException(s"more than one active taskSet for stage $stage:" +
          s" ${stageTaskSets.toSeq.map{_._2.taskSet.id}.mkString(",")}")
      }
      schedulableBuilder.addTaskSetManager(manager, manager.taskSet.properties)

      if (!isLocal && !hasReceivedTask) {
        starvationTimer.scheduleAtFixedRate(new TimerTask() {
          override def run() {
            if (!hasLaunchedTask) {
              logWarning("Initial job has not accepted any resources; " +
                "check your cluster UI to ensure that workers are registered " +
                "and have sufficient resources")
            } else {
              this.cancel()
            }
          }
        }, STARVATION_TIMEOUT_MS, STARVATION_TIMEOUT_MS)
      }
      hasReceivedTask = true
    }
    backend.reviveOffers()
  }
```

* launch任务

```
CoarseGrainedSchedulerBackend:

    // Make fake resource offers on all executors
    private def makeOffers() {
      // Filter out executors under killing
      val activeExecutors = executorDataMap.filterKeys(executorIsAlive)
      val workOffers = activeExecutors.map { case (id, executorData) =>
        new WorkerOffer(id, executorData.executorHost, executorData.freeCores)
      }.toIndexedSeq
      launchTasks(scheduler.resourceOffers(workOffers))
    }
```

* 任务调度及资源分配

`scheduler.resourceOffers(workOffers)`主要完成任务的资源分配。

```
TaskSchedulerImpl:

  /**
   * Called by cluster manager to offer resources on slaves. We respond by asking our active task
   * sets for tasks in order of priority. We fill each node with tasks in a round-robin manner so
   * that tasks are balanced across the cluster.
   */
  def resourceOffers(offers: IndexedSeq[WorkerOffer]): Seq[Seq[TaskDescription]] = synchronized {
///...
    // Take each TaskSet in our scheduling order, and then offer it each node in increasing order
    // of locality levels so that it gets a chance to launch local tasks on all of them.
    // NOTE: the preferredLocality order: PROCESS_LOCAL, NODE_LOCAL, NO_PREF, RACK_LOCAL, ANY
    for (taskSet <- sortedTaskSets) { ///遍历taskSet，为每个taskSet分配资源
      var launchedAnyTask = false
      var launchedTaskAtCurrentMaxLocality = false
      for (currentMaxLocality <- taskSet.myLocalityLevels) {
        do {
          launchedTaskAtCurrentMaxLocality = resourceOfferSingleTaskSet(
            taskSet, currentMaxLocality, shuffledOffers, availableCpus, tasks) ///单个taskSet分配资源
          launchedAnyTask |= launchedTaskAtCurrentMaxLocality
        } while (launchedTaskAtCurrentMaxLocality)
      }
      if (!launchedAnyTask) {
        taskSet.abortIfCompletelyBlacklisted(hostToExecutors)
      }
    }
///...
}

 ///单个taskSet分配资源
  private def resourceOfferSingleTaskSet(
      taskSet: TaskSetManager, ///taskSet
      maxLocality: TaskLocality,
      shuffledOffers: Seq[WorkerOffer], ///Worker节点列表
      availableCpus: Array[Int],
      tasks: IndexedSeq[ArrayBuffer[TaskDescription]]) : Boolean = {
    var launchedTask = false
    // nodes and executors that are blacklisted for the entire application have already been
    // filtered out by this point
    for (i <- 0 until shuffledOffers.size) { ///遍历每个executor
      val execId = shuffledOffers(i).executorId
      val host = shuffledOffers(i).host
      if (availableCpus(i) >= CPUS_PER_TASK) { ///executor还有CPU资源
        try {
          for (task <- taskSet.resourceOffer(execId, host, maxLocality)) {
            tasks(i) += task
            val tid = task.taskId
            taskIdToTaskSetManager(tid) = taskSet ///taskID -> taskSet
            taskIdToExecutorId(tid) = execId  ///taskID -> executor ID
            executorIdToRunningTaskIds(execId).add(tid) ///executor ID -> taskID
            availableCpus(i) -= CPUS_PER_TASK
            assert(availableCpus(i) >= 0)
            launchedTask = true
          }
        } catch {
          case e: TaskNotSerializableException =>
            logError(s"Resource offer failed, task set ${taskSet.name} was not serializable")
            // Do not offer resources for this task, but don't throw an error to allow other
            // task sets to be submitted.
            return launchedTask
        }
      }
    }
    return launchedTask
  }
```


```
2017-08-02 03:10:49 INFO  SparkContext:54 - Created broadcast 1 from broadcast at DAGScheduler.scala:996
2017-08-02 03:10:49 INFO  DAGScheduler:54 - Submitting 80 missing tasks from ShuffleMapStage 0 (MapPartitionsRDD[4] at repartition at tap2.scala:90)
2017-08-02 03:10:49 INFO  KubernetesTaskSchedulerImpl:54 - Adding task set 0.0 with 80 tasks
2017-08-02 03:10:50 INFO  KubernetesTaskSetManager:54 - Starting task 0.0 in stage 0.0 (TID 0, 192.168.24.100, executor 5, partition 0, ANY, 6220 bytes)
2017-08-02 03:10:50 INFO  KubernetesTaskSetManager:54 - Starting task 1.0 in stage 0.0 (TID 1, 192.168.21.196, executor 3, partition 1, ANY, 6220 bytes)
```

```
TaskSetManager:

  @throws[TaskNotSerializableException]
  def resourceOffer(
      execId: String,
      host: String,
      maxLocality: TaskLocality.TaskLocality)
    : Option[TaskDescription] =
  {
///...
        addRunningTask(taskId)

        // We used to log the time it takes to serialize the task, but task size is already
        // a good proxy to task serialization time.
        // val timeTaken = clock.getTime() - startTime
        val taskName = s"task ${info.id} in stage ${taskSet.id}"
        logInfo(s"Starting $taskName (TID $taskId, $host, executor ${info.executorId}, " +
          s"partition ${task.partitionId}, $taskLocality, ${serializedTask.limit} bytes)")
```

* executor执行任务

```
2017-08-02 03:21:02 INFO  CoarseGrainedExecutorBackend:54 - Got assigned task 92
2017-08-02 03:21:02 INFO  CoarseGrainedExecutorBackend:54 - Got assigned task 192
2017-08-02 03:21:02 INFO  CoarseGrainedExecutorBackend:54 - Got assigned task 292
2017-08-02 03:21:02 INFO  CoarseGrainedExecutorBackend:54 - Got assigned task 392
2017-08-02 03:21:02 INFO  Executor:54 - Running task 212.0 in stage 1.0 (TID 292)
2017-08-02 03:21:02 INFO  Executor:54 - Running task 312.0 in stage 1.0 (TID 392)
2017-08-02 03:21:02 INFO  Executor:54 - Running task 112.0 in stage 1.0 (TID 192)
2017-08-02 03:21:02 INFO  Executor:54 - Running task 12.0 in stage 1.0 (TID 92)
```

```
CoarseGrainedExecutorBackend:

    case LaunchTask(data) =>
      if (executor == null) {
        exitExecutor(1, "Received LaunchTask command but executor was null")
      } else {
        val taskDesc = TaskDescription.decode(data.value)
        logInfo("Got assigned task " + taskDesc.taskId)
        executor.launchTask(this, taskDesc)
      }
```


```
  class TaskRunner(
      execBackend: ExecutorBackend,
      private val taskDescription: TaskDescription)
    extends Runnable {
///...
    override def run(): Unit = {
      threadId = Thread.currentThread.getId
      Thread.currentThread.setName(threadName)
      val threadMXBean = ManagementFactory.getThreadMXBean
      val taskMemoryManager = new TaskMemoryManager(env.memoryManager, taskId)
      val deserializeStartTime = System.currentTimeMillis()
      val deserializeStartCpuTime = if (threadMXBean.isCurrentThreadCpuTimeSupported) {
        threadMXBean.getCurrentThreadCpuTime
      } else 0L
      Thread.currentThread.setContextClassLoader(replClassLoader)
      val ser = env.closureSerializer.newInstance()
      logInfo(s"Running $taskName (TID $taskId)")
...
        val value = try {
          val res = task.run( ///Task.run
            taskAttemptId = taskId,
            attemptNumber = taskDescription.attemptNumber,
            metricsSystem = env.metricsSystem)
          threwException = false
          res
        } 
...
```
