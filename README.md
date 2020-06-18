# Solitaire Solver

This is a brute force solver for the 3-card draw variant of [Klondike
solitaire](https://en.wikipedia.org/wiki/Klondike_(solitaire)). It does
some branch pruning via caching previously seen states and a few other
heuristics to avoid loops and exploring the same state twice.

This is a more or less direct translation of the [solver I wrote in
JavaScript](https://github.com/rfotino/solitaire-js) which runs using
node.js. This C++ rewrite gets rid of large string representations of
states and uses almost no dynamic memory allocations to get the most
moves/second out of the CPU.

There is quite a bit of previous literature on the subject of solitaire
solvers, e.g. http://web.engr.oregonstate.edu/~afern/papers/solitaire.pdf
and https://arxiv.org/pdf/1906.12314.pdf. It seems to indicate that my
brute force solution will not be feasible in beating the current
results.

I began this project by losing 20+ games in a row and deciding to test
what percent of shuffles result in solvable games. That turned out to
be quite a hard problem - and in the end most games *do* have a solution,
but that solution would never be found by a human playing a standard
game because you have to make some unusual moves like drawing and playing
from the excess cards when there is a card-revealing move already
available. So for a great many games you miss a winning solution
because you play the most optimal solution that you can actually see,
and then obviously don't backtrack to correct it like this solver does.

# Building

You will need [folly](https://github.com/facebook/folly),
[glog](https://github.com/google/glog), and
[gflags](https://github.com/gflags/gflags) installed in order to build
from source. The ./build.sh command uses `g++` to generate the binary.

# Running

You can use `generate.js` from the
[JavaScript implementation](https://github.com/rfotino/solitaire-js) to
create a file of random deck shuffles. You feed that file to the solver
on stdin, and it will write some logs to stderr and the JSON results of
the games to stdout.

You can use `--timeout N` to set the timeout for each game in seconds,
and `--state_cache_size N`, `--move_cache_size N` to change the number
of objects available in the state or move caches - this is probably
not necessary without a good understanding of the program.

# License

MIT