void wire() {
    QObject::connect(sender, signal, receiver, slot);
    other::connect(sender, signal, receiver, slot);
}
