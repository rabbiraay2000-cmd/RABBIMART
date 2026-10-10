# Build stage: compile with Maven
FROM maven:3.9-eclipse-temurin-17 AS build
WORKDIR /app
COPY pom.xml .
RUN mvn -q dependency:go-offline
COPY src ./src
RUN mvn -q package -DskipTests

# Run stage: small JRE image
FROM eclipse-temurin:17-jre
WORKDIR /app
COPY --from=build /app/target/rabbimart-1.0-SNAPSHOT-jar-with-dependencies.jar app.jar
COPY public ./public
EXPOSE 8080
CMD ["java", "-jar", "app.jar"]
