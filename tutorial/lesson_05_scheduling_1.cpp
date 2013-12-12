// Halide tutorial lesson 5

// This lesson demonstrates how to manipulate the order in which you
// evaluate pixels in a Func, including vectorization,
// parallelization, unrolling, and tiling.

// On linux, you can compile and run it like so:
// g++ lesson_05*.cpp -I ../include -L ../bin -rdynamic -lHalide -lpthread -ldl -o lesson_05
// LD_LIBRARY_PATH=../bin ./lesson_05

// On os x:
// g++ lesson_05*.cpp -I ../include -L ../bin -lHalide -o lesson_05
// DYLD_LIBRARY_PATH=../bin ./lesson_05

#include <Halide.h>
#include <stdio.h>
using namespace Halide;

int main(int argc, char **argv) {

    // We're going to define and schedule our gradient function in
    // several different ways, and see what order pixels are computed
    // in.

    Var x("x"), y("y");

    // First we observe the default ordering.
    {
        Func gradient("gradient");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // By default we walk along the rows and then down the columns.
        printf("Evaluating gradient row-major\n");
        Image<int> output = gradient.realize(4, 4);

        // The equivalent C is:
        printf("Equivalent C:\n");
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                printf("Evaluating at %d, %d: %d\n", x, y, x + y);
            }
        }
        printf("\n\n");
    }

    // Reorder variables.
    {
        Func gradient("gradient_col_major");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // If we reorder x and y, we can walk down the columns
        // instead. The reorder call takes the arguments of the func,
        // and sets a new nesting order for the for loops that are
        // generated. The arguments are specified from the innermost
        // loop out, so the following call puts y in the inner loop:
        gradient.reorder(y, x);

        printf("Evaluating gradient column-major\n");
        Image<int> output = gradient.realize(4, 4);

        printf("Equivalent C:\n");
        for (int x = 0; x < 4; x++) {
            for (int y = 0; y < 4; y++) {
                printf("Evaluating at %d, %d: %d\n", x, y, x + y);
            }
        }
        printf("\n\n");
    }

    // Split a variable into two.
    {
        Func gradient("gradient_split");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // The most powerful primitive scheduling operation you can do
        // to a var is to split it into inner and outer sub-variables:
        Var x_outer, x_inner;
        gradient.split(x, x_outer, x_inner, 2);

        // This breaks the loop over x into two nested loops: an outer
        // one over x_outer, and an inner one over x_inner. The last
        // argument to split was the "split factor". The inner loop
        // runs from zero to the split factor. The outer loop runs
        // from zero to the extent required of x (4 in this case)
        // divided by the split factor. Within the loops, the old
        // variable is defined to be outer * factor + inner. If the
        // old loop started at a value other than zero, then that is
        // also added within the loops.

        printf("Evaluating gradient with x split into x_outer and x_inner \n");
        Image<int> output = gradient.realize(4, 4);

        printf("Equivalent C:\n");
        for (int y = 0; y < 4; y++) {
            for (int x_outer = 0; x_outer < 2; x_outer++) {
                for (int x_inner = 0; x_inner < 2; x_inner++) {
                    int x = x_outer * 2 + x_inner;
                    printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                }
            }
        }
        printf("\n\n");

        // Note that the order of evaluation of pixels didn't actually
        // change! Splitting by itself does nothing, but it does open
        // up all of the scheduling possibilities that we will explore
        // below.
    }

    // Fuse two variables into one.
    {
        Func gradient("gradient_fused");
        gradient(x, y) = x + y;

        // The opposite of splitting is 'fusing'. Fusing two variables
        // merges the two loops into a single for loop over the
        // product of the extents. Fusing is less important that
        // splitting, but it also sees use (as we'll see later in this
        // lesson). Like splitting, fusing by itself doesn't change
        // the order of evaluation.
        Var fused;
        gradient.fuse(x, y, fused);

        printf("Evaluating gradient with x and y fused\n");
        Image<int> output = gradient.realize(4, 4);

        printf("Equivalent C:\n");
        for (int fused = 0; fused < 4*4; fused++) {
            int y = fused / 4;
            int x = fused % 4;
            printf("Evaluating at %d, %d: %d\n", x, y, x + y);
        }
    }

    // Evaluating in tiles.
    {
        Func gradient("gradient_tiled");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // Now that we can both split and reorder, we can do tiled
        // evaluation. Let's split both x and y by a factor of two,
        // and then reorder the vars to express a tiled traversal.
        //
        // A tiled traversal splits the domain into small rectangular
        // tiles, and outermost iterates over the tiles, and within
        // that iterates over the points within each tile. It can be
        // good for performance if neighboring pixels use overlapping
        // input data, for example in a blur. We can express a tiled
        // traversal like so:
        Var x_outer, x_inner, y_outer, y_inner;
        gradient.split(x, x_outer, x_inner, 2);
        gradient.split(y, y_outer, y_inner, 2);
        gradient.reorder(x_inner, y_inner, x_outer, y_outer);

        // This pattern is common enough that there's a shorthand for it:
        // gradient.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2);

        printf("Evaluating gradient in 2x2 tiles\n");
        Image<int> output = gradient.realize(4, 4);

        printf("Equivalent C:\n");
        for (int y_outer = 0; y_outer < 2; y_outer++) {
            for (int x_outer = 0; x_outer < 2; x_outer++) {
                for (int y_inner = 0; y_inner < 2; y_inner++) {
                    for (int x_inner = 0; x_inner < 2; x_inner++) {
                        int x = x_outer * 2 + x_inner;
                        int y = y_outer * 2 + y_inner;
                        printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                    }
                }
            }
        }
        printf("\n\n");
    }

    // Evaluating in vectors.
    {
        Func gradient("gradient_in_vectors");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // The nice thing about splitting is that it guarantees the
        // inner variable runs from zero to the split factor. Most of
        // the time the split-factor will be a compile-time constant,
        // so we can replace the loop over the inner variable with a
        // single vectorized computation. This time we'll split by a
        // factor of four, because on X86 we can use SSE to compute in
        // 4-wide vectors.
        Var x_outer, x_inner;
        gradient.split(x, x_outer, x_inner, 4);
        gradient.vectorize(x_inner);

        // Splitting and then vectorizing the inner variable is common
        // enough that there's a short-hand for it. We could have also
        // said:
        //
        // gradient.vectorize(x, 4);
        //
        // which is equivalent to:
        //
        // gradient.split(x, x, x_inner, 4);
        // gradient.vectorize(x_inner);
        //
        // Note that in this case we reused the name 'x' as the new
        // outer variable. Later scheduling calls that refer to x
        // will refer to this new outer variable named x.
        //
        // Our snoop function isn't set-up to print out vectors, this
        // is why we included one called snoopx4 above.

        // This time we'll evaluate over an 8x4 box, so that we have
        // more than one vector of work per scanline.
        printf("Evaluating gradient with x_inner vectorized \n");
        Image<int> output = gradient.realize(8, 4);

        printf("Equivalent C:\n");
        for (int y = 0; y < 4; y++) {
            for (int x_outer = 0; x_outer < 2; x_outer++) {
                // The loop over x_inner has gone away, and has been
                // replaced by a vectorized version of the
                // expression. On x86 processors, Halide generates SSE
                // for all of this.
                int x_vec[] = {x_outer * 4 + 0,
                               x_outer * 4 + 1,
                               x_outer * 4 + 2,
                               x_outer * 4 + 3};
                int val[] = {x_vec[0] + y,
                             x_vec[1] + y,
                             x_vec[2] + y,
                             x_vec[3] + y};
                printf("Evaluating at <%d, %d, %d, %d>, <%d, %d, %d, %d>: <%d, %d, %d, %d>\n",
                       x_vec[0], x_vec[1], x_vec[2], x_vec[3],
                       y, y, y, y,
                       val[0], val[1], val[2], val[3]);
            }
        }
        printf("\n\n");
    }

    // Unrolling a loop.
    {
        Func gradient("gradient_in_vectors");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // If multiple pixels share overlapping data, it can make
        // sense to unroll a computation so that shared values are
        // only computed or loaded once. We do this similarly to how
        // we expressed vectorizing. We split a dimension and then
        // fully unroll the loop of the inner variable. Unrolling
        // doesn't change the order in which things are evaluated.
        Var x_outer, x_inner;
        gradient.split(x, x_outer, x_inner, 2);
        gradient.unroll(x_inner);

        // The shorthand for this is:
        // gradient.unroll(x, 2);

        printf("Evaluating gradient unrolled by a factor of two\n");
        Image<int> result = gradient.realize(4, 4);

        printf("Equivalent C:\n");
        for (int y = 0; y < 4; y++) {
            for (int x_outer = 0; x_outer < 2; x_outer++) {
                // Instead of a for loop over x_inner, we get two
                // copies of the innermost statement.
                {
                    int x_inner = 0;
                    int x = x_outer * 2 + x_inner;
                    printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                }
                {
                    int x_inner = 1;
                    int x = x_outer * 2 + x_inner;
                    printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                }
            }
        }


    }

    // Splitting by factors that don't divide the extent.
    {
        Func gradient("gradient_split_5x4");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // Splitting guarantees that the inner loop runs from zero to
        // the split factor, which is important for the uses we saw
        // above. So what happens when the total extent we wish to
        // evaluate x over isn't a multiple of the split factor? We'll
        // split by a factor of two again, but now we'll evaluate
        // gradient over a 5x4 box instead of the 4x4 box we've been
        // using.
        Var x_outer, x_inner;
        gradient.split(x, x_outer, x_inner, 2);

        printf("Evaluating gradient over a 5x4 box with x split by two \n");
        Image<int> output = gradient.realize(5, 4);

        printf("Equivalent C:\n");
        for (int y = 0; y < 4; y++) {
            for (int x_outer = 0; x_outer < 3; x_outer++) { // Now runs from 0 to 3
                for (int x_inner = 0; x_inner < 2; x_inner++) {
                    int x = x_outer * 2;
                    // Before we add x_inner, make sure we don't
                    // evaluate points outside of the 5x4 box. We'll
                    // clamp x to be at most 3 (5 minus the split
                    // factor).
                    if (x > 3) x = 3;
                    x += x_inner;
                    printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                }
            }
        }
        printf("\n\n");

        // If you read the output, you'll see that some coordinates
        // were evaluated more than once! That's generally OK, because
        // pure Halide functions have no side-effects, so it's safe to
        // evaluate the same point multiple times. If you're calling
        // out to C functions like we are, it's your responsibility to
        // make sure you can handle the same point being evaluated
        // multiple times.

        // The general rule is: If we require x from x_min to x_min + x_extent, and
        // we split by a factor 'factor', then:
        //
        // x_outer runs from 0 to (x_extent + factor - 1)/factor
        // x_inner runs from 0 to factor
        // x = min(x_outer * factor, x_extent - factor) + x_inner + x_min
        //
        // In our example, x_min was 0, x_extent was 5, and factor was 2.
    }

    // Fusing, tiling, and parallelizing.
    {
        // We saw in the previous lesson that we can parallelize
        // across a variable. Here we combine it with fusing and
        // tiling to express a useful pattern - processing tiles in
        // parallel.

        // This is where fusing shines. Fusing helps when you want to
        // parallelize across multiple dimensions without introducing
        // nested parallelism. Nested parallelism (parallel for loops
        // within parallel for loops) is supported by Halide, but
        // often gives poor performance compared to fusing the
        // parallel variables into a single parallel for loop.

        Func gradient("gradient_fused_tiles");
        gradient(x, y) = x + y;
        gradient.trace_stores();

        // First we'll tile, then we'll fuse the tile indices and
        // parallelize across the combination.
        Var x_outer, y_outer, x_inner, y_inner, tile_index;
        gradient.tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2);
        gradient.fuse(x_outer, y_outer, tile_index);
        gradient.parallel(tile_index);

        // The scheduling calls all return a reference to the Func, so
        // you can also chain them together into a single statement to
        // make things slightly clearer:
        //
        // gradient
        //     .tile(x, y, x_outer, y_outer, x_inner, y_inner, 2, 2)
        //     .fuse(x_outer, y_outer, tile_index)
        //     .parallel(tile_index);


        printf("Evaluating gradient tiles in parallel\n");
        Image<int> output = gradient.realize(4, 4);

        // The tiles should occur in arbitrary order, but within each
        // tile the pixels will be traversed in row-major order.

        printf("Equivalent (serial) C:\n");
        // This outermost loop should be a parallel for loop, but that's hard in C.
        for (int tile_index = 0; tile_index < 4; tile_index++) {
            int y_outer = tile_index / 2;
            int x_outer = tile_index % 2;
            for (int y_inner = 0; y_inner < 2; y_inner++) {
                for (int x_inner = 0; x_inner < 2; x_inner++) {
                    int y = y_outer * 2 + y_inner;
                    int x = x_outer * 2 + x_inner;
                    printf("Evaluating at %d, %d: %d\n", x, y, x + y);
                }
            }
        }
        printf("\n\n");
    }

    // Putting it all together.
    {
        // Are you ready? We're going to use all of the features above now.
        Func gradient_fast("gradient_fast");
        gradient_fast(x, y) = x + y;

        // We'll process 256x256 tiles in parallel.
        Var x_outer, y_outer, x_inner, y_inner, tile_index;
        gradient_fast
            .tile(x, y, x_outer, y_outer, x_inner, y_inner, 256, 256)
            .fuse(x_outer, y_outer, tile_index)
            .parallel(tile_index);

        // We'll compute two scanlines at once while we walk across
        // each tile. We'll also vectorize in x. The easiest way to
        // express this is to recursively tile again within each tile
        // into 4x2 subtiles, then vectorize the subtiles across x and
        // unroll them across y:
        Var x_inner_outer, y_inner_outer, x_vectors, y_pairs;
        gradient_fast
            .tile(x_inner, y_inner, x_inner_outer, y_inner_outer, x_vectors, y_pairs, 4, 2)
            .vectorize(x_vectors)
            .unroll(y_pairs);

        // Note that we didn't do any explicit splitting or
        // reordering. Those are the most important primitive
        // operations, but mostly they are buried underneath tiling,
        // vectorizing, or unrolling calls.

        // Now let's evaluate this over a range which is not a
        // multiple of the tile size.

        // If you like you can turn on tracing, but it's going to
        // produce a lot of printfs. Instead we'll compute the answer
        // both in C and Halide and see if the answers match.
        Image<int> result = gradient_fast.realize(800, 600);

        printf("Checking Halide result against equivalent C...\n");
        for (int tile_index = 0; tile_index < 4 * 3; tile_index++) {
            int y_outer = tile_index / 4;
            int x_outer = tile_index % 4;
            for (int y_inner_outer = 0; y_inner_outer < 256/2; y_inner_outer++) {
                for (int x_inner_outer = 0; x_inner_outer < 256/4; x_inner_outer++) {
                    // We're vectorized across x
                    int x = std::min(x_outer * 256, 800-256) + x_inner_outer*4;
                    int x_vec[4] = {x + 0,
                                    x + 1,
                                    x + 2,
                                    x + 3};

                    // And we unrolled across y
                    int y_base = std::min(y_outer * 256, 600-256) + y_inner_outer*2;
                    {
                        // y_pairs = 0
                        int y = y_base + 0;
                        int y_vec[4] = {y, y, y, y};
                        int val[4] = {x_vec[0] + y_vec[0],
                                      x_vec[1] + y_vec[1],
                                      x_vec[2] + y_vec[2],
                                      x_vec[3] + y_vec[3]};

                        // Check the result.
                        for (int i = 0; i < 4; i++) {
                            if (result(x_vec[i], y_vec[i]) != val[i]) {
                                printf("There was an error at %d %d!\n", x_vec[i], y_vec[i]);
                                return -1;
                            }
                        }
                    }
                    {
                        // y_pairs = 1
                        int y = y_base + 1;
                        int y_vec[4] = {y, y, y, y};
                        int val[4] = {x_vec[0] + y_vec[0],
                                      x_vec[1] + y_vec[1],
                                      x_vec[2] + y_vec[2],
                                      x_vec[3] + y_vec[3]};

                        // Check the result.
                        for (int i = 0; i < 4; i++) {
                            if (result(x_vec[i], y_vec[i]) != val[i]) {
                                printf("There was an error at %d %d!\n", x_vec[i], y_vec[i]);
                                return -1;
                            }
                        }
                    }
                }
            }
        }
    }

    // Note that in the Halide version, the algorithm is specified
    // once at the top, separately from the optimizations, and there
    // aren't that many lines of code total. Compare this to the C
    // version. There's more code (and it isn't even parallelized or
    // vectorized properly). More annoyingly, the statement of the
    // algorithm (the result is x plus y) is buried in multiple places
    // within the mess. This C code is hard to write, hard to read,
    // hard to debug, and hard to optimize further. This is why Halide
    // exists.

    printf("Success!\n");
    return 0;
}
