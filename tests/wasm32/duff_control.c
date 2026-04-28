int duff_count(int count)
{
    int n = (count + 7) / 8;
    int sum = 0;

    switch (count % 8) {
    case 0:
        do {
            sum += 8;
    case 7:
            sum += 7;
    case 6:
            sum += 6;
    case 5:
            sum += 5;
    case 4:
            sum += 4;
    case 3:
            sum += 3;
    case 2:
            sum += 2;
    case 1:
            sum += 1;
        } while (--n > 0);
    }

    return sum;
}
