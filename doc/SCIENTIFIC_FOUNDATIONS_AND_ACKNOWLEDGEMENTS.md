# Scientific foundations, references and acknowledgements

This page preserves the textbook references, research-paper links,
acknowledgements and development credits from the original S7 project README.
For the project overview, see the [S7 README](../README.md).

## Scientific foundations and thanks

We are grateful to **William McC. Siebert** for the book that inspired S7, and
to the MIT educators who have carried this signals-and-systems teaching
tradition forward. In particular, we thank **Dennis Freeman and Sanjoy Mahajan**
for their explanations of difference equations, operators, feedback and
discrete approximations, and **Alan V. Oppenheim, Alan S. Willsky and
S. Hamid Nawab** for their signals-and-systems textbook. We also thank
**Nancy Lynch** for her teaching on reasoning about distributed systems.

Beyond MIT, the work of **Edward A. Lee, Sanjit A. Seshia, David G. Messerschmitt,
Thomas M. Parks and the Ptolemy researchers**, together with **Leslie Lamport's**
work on causality, informs our study of dataflow, bounded execution and
distributed computation. The original publications and teaching materials are:

- **William McC. Siebert**, *Circuits, Signals, and Systems*. MIT Press, 1985.
  [Publisher and editions](https://mitpress.mit.edu/9780262690959/circuits-signals-and-systems/)
  (paperback ISBN 978-0-262-69095-9),
  [book DOI](https://doi.org/10.7551/mitpress/1839.001.0001).
  The epigraph excerpt in the [project README](../README.md) is on p. v; chapters 7–8 inform our treatment of
  discrete-time block networks and integrator realizations.
- **Sanjoy Mahajan and Dennis Freeman**, *Discrete-time Signals and Systems:
  An Operator Approach* (2009 notes), used in
  [MIT 6.003, Fall 2011](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/pages/readings/).
  The [complete original notes](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/7b3f66736bb503468103ce8a55fe16e0_MIT6_003F11_notes.pdf)
  develop block diagrams and operators in chapter 3 and discrete integration
  through an oscillator in chapter 6.
- **Alan V. Oppenheim, Alan S. Willsky and S. Hamid Nawab**,
  [*Signals and Systems*, second edition](https://www.pearson.com/en-us/subject-catalog/p/signals-and-systems/P200000003155?view=educator).
  Prentice Hall, 1997 (published 1996), ISBN 978-0-13-814757-0.
  This is also the textbook listed for MIT 6.003.
- **Nancy A. Lynch**, [*Distributed Algorithms*](https://groups.csail.mit.edu/tds/distalgs.html).
  Morgan Kaufmann, 1996, ISBN 978-1-55860-348-6. Her
  [MIT 6.852J course](https://ocw.mit.edu/courses/6-852j-distributed-algorithms-fall-2009/pages/lecture-notes/)
  supplied teaching material for our causality and deadlock investigation.
- **Edward A. Lee and Sanjit A. Seshia**,
  [*Introduction to Embedded Systems: A Cyber-Physical Systems Approach*](https://ptolemy.berkeley.edu/books/leeseshia/).
  Second edition, MIT Press, 2017, ISBN 978-0-262-53381-2; especially §6.3
  on dataflow and initialized feedback.
- **Edward A. Lee and David G. Messerschmitt**,
  [“Synchronous Data Flow”](https://ptolemy.berkeley.edu/publications/papers/87/synchdataflow/).
  *Proceedings of the IEEE* 75(9), 1235–1245, 1987,
  [DOI](https://doi.org/10.1109/PROC.1987.13876); and **Thomas M. Parks**,
  [*Bounded Scheduling of Process Networks*](https://ptolemy.berkeley.edu/publications/papers/95/parksThesis/),
  PhD dissertation, UC Berkeley, 1995.
- **Leslie Lamport**,
  [“Time, Clocks, and the Ordering of Events in a Distributed System”](https://lamport.org/pubs/time-clocks.pdf).
  *Communications of the ACM* 21(7), 558–565, 1978,
  [DOI](https://doi.org/10.1145/359545.359563).

The [integrator reading record](../s7l2/docs/INTEGRATOR_REFERENCES.md) and
[dataflow/causality research record](../s7l2/docs/CROSS_NODE_CAUSALITY_RESEARCH.md)
identify the sections consulted, what we learned from them and the limits of
their applicability to S7. These acknowledgments recognize intellectual
influences, rather than institutional affiliation or endorsement.

We also thank the researchers, engineers and maintainers of the free software
on which S7 depends. Their work makes an inspectable, extensible research tool
possible; [licensing and third-party credits](../LICENSING.md) identify the components.

## Development with AI tools

Thanks to ChatGPT and Codex (OpenAI), Claude Code (Anthropic), and GitHub Copilot for assistance with design, implementation, testing and documentation. The maintainer directs and reviews this work.

Many thanks to the companies and engineers who build these AI tools: they make development a real pleasure.
