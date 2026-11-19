message(FATAL_ERROR
    "Source packaging from the working directory is disabled because it can include private local files. "
    "Use git archive on a reviewed commit instead. Binary packaging remains available.")
