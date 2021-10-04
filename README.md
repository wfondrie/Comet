# Comet
Comet is an open source tandem mass spectrometry (MS/MS) sequence database search engine. It identifies peptides by searching MS/MS spectra against sequences present in protein sequence databases.

Comet currently exists as a simple Windows or Linux command line binary that only does MS/MS database search. Supported input formats are mzXML, mzML, ms2, and Thermo RAW files. Supported output formats are tab-delimited text, Percolator pin, SQT, and pepXML

Documentation and project website: http://comet-ms.sourceforge.net

## Background
Searching uninterpreted tandem mass spectra of peptides against sequence databases is the most common method used to identify peptides and proteins. Since this method was first developed in 1993, many commercial, free, and open source tools have been created over the years that accomplish this task.

Although its history goes back two decades, the Comet search engine was first made publicly available in August 2012 on SourceForge under the Apache License, version 2.0. Comet is multithreaded, supports multiple input and output formats, and binaries are available for both Windows and Linux operating systems.

Note that Comet is just a single command line binary that does MS/MS database search. It takes in spectra in some supported input format and writes out .pep.xml, .pin.xml, .sqt and/or .out files. You will need some other support tool(s) to actually make use of Comet results.

Publications
A Deeper Look into Comet - Implementation and Features. Eng JK, Hoopmann MR, Jahan TA, Egertson JD, Noble WS, MacCoss MJ. J Am Soc Mass Spectrom. 2015 Jun 27. doi: [10.1007/s13361-015-1179-x](https://doi.org/10.1007/s13361-015-1179-x)

Comet: an open source tandem mass spectrometry sequence database search tool. Eng JK, Jahan TA, Hoopmann MR. Proteomics. 2012 Nov 12. doi: [10.1002/pmic.201200439](https://doi.org/10.1002/pmic.201200439)

>I know you're smart. But everyone here is smart. Smart isn't enough. The kind of people I want on my research team are those who will help everyone feel happy to be here.   - Randy Pausch, The Last Lecture
