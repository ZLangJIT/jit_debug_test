p=$1
s=$2
find $p -type f -exec bash -c "echo \"{}\" | grep -E -q -v \"/(include|cmake|perl4|perl5|perl6)/\" && (echo 'testing llvm-nm -D \"{}\"' && if llvm-nm -D \"{}\" |& grep -E \"$s\" |& grep -v 'no symbols' |& grep -v 'file format' |& grep -E \"$s\" --color; then echo 'contains symbols: \"{}\"' ; fi ; echo 'testing llvm-objdump -T \"{}\"' && if llvm-objdump -T \"{}\" |& grep -E \"$s\" |& grep -v 'no symbols' |& grep -v 'file format' |& grep -E \"$s\" --color; then echo 'contains symbols: \"{}\"' ; fi) || (echo 'skipping \"{}\"')" \;
