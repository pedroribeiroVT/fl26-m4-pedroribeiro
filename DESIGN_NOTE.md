# M4 Design Note

## 1. Components and threads
- `MainWindow` runs only on the GUI thread. It owns the workspace, the current `ProcessingCore`, the corpus state and a build number called `build_id_`. A `QTimer` checks on the running build. All widget changes happen here.
- `CorpusBuilder` is a new class with no Qt in it. It's only called from the GUI thread, and it owns one `std::thread`, a `std::future` for the result and a `std::atomic<bool>` cancel flag.
- The background thread runs one function. It gets a copy of the workspace, the delay and an empty `ProcessingCore`. It sleeps for the delay times the number of documents in 10 ms steps, stopping early if cancel is set. Then it calls `rebuild()` and sends back the corpus, or the exception if anything throws. It never touches Qt.

## 2. Corpus states
The corpus is always NotCurrent, Building or Current.
- Adding a document or clicking Reset moves to NotCurrent. If a build is running, it also increments `build_id_` and sets cancel.
- Build Corpus is only enabled when there are documents and no build is running. It increments `build_id_`, starts a build and moves to Building.
- When a build succeeds and its id matches `build_id_`, the state becomes Current and the chunk count is shown.
- When a build fails and its id matches `build_id_`, the error is shown and the state becomes NotCurrent.
- Any result with an old id is thrown away and the state doesn't change.

## 3. Sequences that threaten R1 to R4
- The user adds a document or clicks Reset during a build, and the build finishes later. The id check throws the old result away, which protects R2.
- The user can't start a second build during a build because Build is disabled, so there's never more than one thread. This covers R1 and R4.
- The build throws. The exception comes back through the future and the error is shown, which covers R3.
- The user exits during a slow build. The builder sets cancel and joins the thread, and the 10 ms steps keep the join short. This covers R4.
- The user builds many times in a row. Each thread is joined when its result is taken, and the old corpus is freed when a new one replaces it. This also covers R4.

**Walkthrough A: adding a document during a build**
1. On the GUI thread, the user builds with 2 documents. `build_id_` becomes 1 and the state is Building.
2. On the background thread, the build is sleeping or rebuilding.
3. On the GUI thread, the user adds a 3rd document. `build_id_` becomes 2, cancel is set and the state is NotCurrent.
4. On the background thread, the build stops early or finishes and sends back its result. A cancelled build sends back an empty result.
5. On the GUI thread, the timer sees the result and the thread is joined. The result has id 1 but `build_id_` is 2, so it's thrown away. Build is enabled again.

**Walkthrough B: exiting during a slow build with `--build-delay-ms 2000`**
1. On the GUI thread, the user builds and the background thread starts sleeping.
2. On the GUI thread, the user clicks Exit and confirms, and the event loop ends.
3. On the GUI thread, `MainWindow` is destroyed. Its `CorpusBuilder` sets cancel and joins the thread.
4. On the background thread, the next 10 ms check sees cancel and the thread ends without building.
5. On the GUI thread, the join returns and then the widgets are destroyed. Nothing crashes because the background thread never had a pointer to them.

## 4. Ownership of one corpus
The builder creates an empty `std::unique_ptr<ProcessingCore>` and moves it into the background thread, so only that thread owns it. After `rebuild()` fills it, it's moved into the future. The GUI thread moves it out of the future. If it's current, it's moved into `MainWindow`, which destroys the previous corpus. If it's stale, it's destroyed right away. The current corpus is destroyed by the next successful build or when `MainWindow` is destroyed. It has one owner at every point and there's no raw `new` or `delete`.

## 5. Rejected alternative
I rejected calling `rebuild()` on the existing `ProcessingCore` from the background thread. `rebuild()` overwrites the chunks and index while Search can be reading them on the GUI thread. That's a data race I couldn't fix without a mutex, and a stale build would overwrite a good corpus. Building into a separate `ProcessingCore` and only swapping it in on the GUI thread avoids both problems.
