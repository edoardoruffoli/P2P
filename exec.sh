# 1. COMPILAZIONE

  make

  read -p "Compilazione eseguita. Premi invio per eseguire..."

# 2. ESECUZIONE

gnome-terminal -- bash -c "(echo 'start 127.0.0.1 4242' && cat) | ./peer 4240; exec bash"
gnome-terminal -- bash -c "(echo 'start 127.0.0.1 4242' && cat) | ./peer 4241; exec bash"
gnome-terminal -- bash -c "(echo 'start 127.0.0.1 4242' && cat) | ./peer 4243; exec bash"
gnome-terminal -- bash -c "(echo 'start 127.0.0.1 4242' && cat) | ./peer 4248; exec bash"
gnome-terminal -- bash -c "(echo 'start 127.0.0.1 4242' && cat) | ./peer 4249; exec bash"

gnome-terminal -- bash -c "./ds 4242; exec bash"
