# M5 Design Record

## 1. M4 fixes

After a failed build, old search results could stay on screen, so starting a build now clears them and a test checks it. One of my M4 tests also checked a stale build before it finished, so it proved nothing. It now waits for the old build to end.

## 2. Threads and shared data

There are three threads. The GUI thread runs `MainWindow`, the states and every slot. One build worker runs `CorpusBuilder::run` from the first build until exit. The IPC thread serves `aiws_send`.

The GUI and the worker share four things: the pending request, the finished result, a generation number and a stop flag. One mutex in `CorpusBuilder` guards all four, and the worker sleeps on a condition variable until one changes. The core and the Workspace copy move through the request and the result, so they have one owner at a time. The GUI and the IPC thread only share a wake pipe the GUI writes a byte to on stop. Both threads reach the GUI through queued signals.

## 3. States

There are four state classes. Each event method returns the next state and `MainWindow` swaps it in.

- `EmptyState`: no documents, Build disabled. Adding a document goes to `OutOfDateState`.
- `OutOfDateState`: documents but no usable corpus. Build goes to `BuildingState`.
- `BuildingState`: a wanted build is running, Build disabled. Success goes to `CurrentState`, failure back to `OutOfDateState`. Adding a document cancels the build and goes to `OutOfDateState`.
- `CurrentState`: the corpus matches the Workspace, so Search, Inspect and Build Context work. Adding a document goes to `OutOfDateState`, and Build starts a new build.

Reset from any state cancels a running build and goes to `EmptyState`. Exit is the same in every state, since the `CorpusBuilder` destructor stops and joins the worker.

## 4. Reset during a long build

1. GUI: Build in `OutOfDateState` sends the request with generation 1 and notifies the worker.
2. Worker: it takes the request and waits out the first document's delay.
3. GUI: Reset in `BuildingState` cancels, so the generation goes to 2, the pending request is cleared and the worker is notified. The state becomes `EmptyState`.
4. Worker: it wakes right away because its build is generation 1, skips the rebuild and sleeps again.
5. GUI: the next Build starts immediately.

## 5. Decisions

1. Build during a build: not allowed. `BuildingState` keeps Build disabled, because rebuilding the same Workspace gains nothing.
2. Remote add mid-build: same as Add Document. The build is cancelled and the corpus shows "Out of date" until the user builds again.
3. Shared data: the four values in item 2. The worker needs its input, has to find out about Reset, Add and Exit, and has to hand back its output.

## 6. Fault switches

`AIWS_FAULT_NO_CANCEL` makes the worker's delay wait ignore the generation change, so a useless build sleeps through its whole delay. The two tests that Reset or Add during a long build catch it.

`AIWS_FAULT_SILENT_SHUTDOWN` removes the notify in the `CorpusBuilder` destructor, so an idle worker never wakes and the join hangs. The first test, "exiting with no build running returns quickly", hangs and CTest's 30 second timeout fails it.

## 7. AI-use disclosure

I used AI to plan each part of M5 and check every plan against the spec before writing code. I also used it to write the new tests in the same style as my previous ones, and to pull examples from my previous milestones to quickly locate how things were implemented.