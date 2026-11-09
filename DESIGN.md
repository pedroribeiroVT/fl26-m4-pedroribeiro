# M4 Decision Record

## 1. Threads and ownership

The app has three threads. The GUI thread runs `MainWindow` and is the only one that touches the screen. A build thread, owned by `CorpusBuilder`, builds the corpus in the background. An IPC thread, owned by `IpcServer`, listens for requests from `aiws_send`. Each owner joins its thread when it is done with it, and none are detached. When the window closes, the IPC server stops first, then any running build is cancelled and joined, and only then are the widgets destroyed.

Each build works on its own copy of the workspace and its own empty corpus, so nothing it touches is shared while it runs. When it finishes, the corpus is handed back to the GUI thread. The window keeps it only if it still matches the workspace; otherwise it is thrown away. A corpus has one owner at every point.

Data shared between threads, and why it is safe:
- The build's cancel flag. The GUI sets it and the build thread checks it. It is atomic.
- The build result. The build thread hands it over once through a promise and future, and the GUI picks it up. The standard library makes that hand-off safe.
- The server's stop flag. The GUI sets it and the IPC thread checks it. It is atomic.
- The server's socket. It is set up before the IPC thread starts, and the GUI only touches it again after the thread has been joined.
- A received path. The IPC thread never calls into the window. It emits a signal that Qt queues and delivers on the GUI thread.

## 2. How a finished build is judged valid

Every build is given a number. Adding a document or clicking Reset while a build is running bumps the number and tells the build to stop early. A timer on the GUI thread checks for a finished build. If its number is not the latest one, it is out of date and is ignored. If the number matches and the build succeeded, the corpus becomes current and its chunk count is shown. If it matches but the build failed, the error is shown and the corpus stays not current. Build Corpus is disabled while a build runs, so there is never more than one.

## 3. Changes since the checkpoint

The design is the same as in `DESIGN_NOTE.md`. I only added a way for tests to swap in a core that fails or runs slowly, plus two signals that report when a build finishes and when the corpus state changes. I also treat any build that comes back without a corpus as a failure. That way a cancelled build can never leave the app saying "current" with nothing to search.

## 4. CR-1 impact

The only part of the window that changed is adding documents. Add Document was split so the file-loading part (`add_document_file()`) can be called both by the button and by a remote request. The window also gained a way to start the IPC server, and `main()` now reads the `--ipc-socket` and `--build-delay-ms` options. The background build did not change at all. A remote add goes through the same path as the button, so it makes a running build out of date in the same way. The server answers `aiws_send` from its own thread without waiting for the GUI. If the file was already added, it is ignored, the same as with the button.

Message format, over a Unix domain socket:
- Request: 4 magic bytes (`6767`), then the path length as a 4-byte big-endian number, then the path in UTF-8. Because the length comes first, spaces and newlines in a path are fine. The server keeps reading until it has the whole message, so a request sent in pieces still works.
- Reply: 1 byte, `0` for accepted or `1` for rejected (unreadable file or bad message).
- `aiws_send` turns a relative path into an absolute one before sending. Both programs default to `/tmp/aiws-$USER.sock`, or the numeric user id if `$USER` is not set.

## 5. Fault switches

- `AIWS_FAULT_ACCEPT_STALE` is in `MainWindow::poll_build()`. It forces `stale = false`, so an old result is accepted. It is caught by "a build that finishes after a document was added is rejected" and "a build that finishes after Reset is rejected". Both use `SlowChunker` so the old build finishes with a real core after the workspace changed.
- `AIWS_FAULT_LOSE_FAILURE` is in `CorpusBuilder::take()`. It clears the error and sets the id to 0, so `poll_build()` throws the result away and the window never leaves `Building`. It is caught by "a failing build returns its error message" and "a failed build leaves the building state".

## 6. AI-use disclosure

I used AI to help connect CR-1 code to `MainWindow` to save time as I spent a lot of time working on my partner's macihine, to find and fix bugs I found along the way. I also used it to write `student_tests.cpp` in the same style as my previous tests, and to help write this file partially by pulling the specific examples from the code to reference.

## Pair Programming Log
Partner Name: Arda Serhatli

Contribution as navigator: 
- Planned out size of protocol being trasmitted between client and server to make it simple
- Look at example code to see which protocols were needed
- In session 2, after writing the server side I had a better hands-on understanding of the socket library, so I helped Arda through the socket calls while Arda wrote the client side.

Partner's contribution as navigator:
- Arda helped with the actual code more than the planning. Getting past the first barrier of understanding POSIX sockets took a lot of effort, and Arda looked up many examples online and dug into the library to be able to help me when I got stuck on the protocol and server code.

Session details:
- Session 1 (Nov 8th, 1:00pm - 2:40pm): I started as navigator and planned the headers while Arda drove and wrote them. About halfway through we swapped. I drove and wrote the protocol implementation and started the server, while Arda looked through example code and helped me with specific socket library logic.
- Session 2 (Nov 8th, 9:00pm - 10:30pm): I started as driver and finished the server implementation while Arda navigated the same way as before. About halfway through we swapped. Arda drove and completed aiws_send, then finished up by adding the argument reading in main and connecting the signal to MainWindow in Arda's own project, and I navigated.

All of the CR-1 code above was written together on Arda's computer during these two sessions. Connecting it to MainWindow was the one part we each did independently in our own projects.

What worked best:
Arda and I live together, so we worked side by side on Arda's computer in our living room, and being physically present worked much better for both of us than a video call would have.