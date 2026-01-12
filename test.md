<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
**Table of Contents**

- [Test](#test)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# Test

ist ja doof

```mermaid
graph TD
    A[Start] --> B(Parse Config)
    B --> C{Scan Input Dir}
    C -->|Found .md| D[Build Directory Tree (DirNode)]
    D --> E[Sort Nodes & Files]
    E --> F[Copy Assets recursively]
    F --> G[Process Files (Recursion)]

    subgraph "Per File Processing"
        G --> H[Calculate Relative Path ../]
        H --> I[Generate Nav HTML with Active State]
        I --> J[Render Markdown to HTML]
        J --> K[Prepare JSON Data]
        K --> L[Render Inja Template]
        L --> M[Write .html Output]
    end

    M --> N[End]
```

und?
