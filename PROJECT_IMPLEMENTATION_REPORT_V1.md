## Project Implementation Report: HTTP Facade Service

### 1. Overview

The challenges involved provided significant learning opportunities, particularly in designing and implementing robust, concurrent network services.

### 2. Implementation Approaches & Current Status

Two primary architectural approaches were explored and implemented, each residing in its own Git branch:

*   **Synchronous Solution (cpp-httplib based):**
    *   **Feature Branch:** `sync-solution`
    *   **Description:** This initial implementation utilizes the `cpp-httplib` library, employing a thread-per-request model for concurrency.
    *   **Latest Observations:** Initial tests indicate that this version achieves approximately 10% SLA compliance while handling a load equivalent to 20% of the anticipated peak.

*   **Asynchronous Solution (Boost.Asio & Boost.Beast based):**
    *   **Feature Branch:** `async-boost`
    *   **Description:** Recognizing the limitations of the synchronous model for high concurrency, this version was developed using Boost.Asio and Boost.Beast to create a fully asynchronous, event-driven architecture.
    *   **Latest Observations (for async-boost/master):** Under similar load conditions (30% of anticipated peak), this version also shows approximately 10% SLA compliance. Functional tests indicate upto 25% correct responses for a specific complex test suite in a couple of runs, suggesting areas for further debugging or refinement in request/response handling logic under this new architecture.

*   **Current `master` Branch Status:** 
    *   The `master` branch currently incorporates this asynchronous solution (feature-branch : `async-boost`).

**Detailed Design Documentation:**

Each branch contains a comprehensive `CODE_DESIGN.md` file detailing the specific architectural decisions, dependencies, performance considerations, and limitations pertinent to that implementation.

### 3. Development Journey & Path Forward

The development process began with the synchronous solution to quickly establish a functional baseline. However, anticipating the performance requirements and the nature of I/O-bound operations, the project pivoted towards the asynchronous architecture to better address scalability and concurrency demands.

While the current performance metrics for both solutions are below the target SLA (95% of requests within 1 second), significant groundwork has been laid, particularly with the asynchronous model.

To address the performance gap and improve overall stability, a detailed section titled **"Debugging / Performance Tuning in Production"** has been documented within the `CODE_DESIGN.md` file (present in both branches, but most relevant to the asynchronous version). This section outlines:
*   Strategies for indirect observation and metrics-based tuning, given the limited direct access to production logs.
*   The importance of gaining deeper contextual understanding (client expectations, backend API contracts, production architecture).
*   Specific areas for code optimization and potential enhancements.

Further improvements will focus on meticulous profiling, refining the asynchronous logic, addressing any functional discrepancies in the asynchronous version, and systematically applying the tuning strategies outlined. The challenges associated with limited production visibility are acknowledged, and the proposed debugging methods aim to work effectively within these constraints.